#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

using boost::asio::ip::tcp;

// Funções auxiliares
std::time_t convert_string_to_time(const std::string& time_str) {
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

std::string convert_time_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

std::vector<std::string> tokenize_string(const std::string& input_str, const char delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    size_t found;
    while ((found = input_str.find(delimiter, pos)) != std::string::npos) {
        tokens.push_back(input_str.substr(pos, found - pos));
        pos = found + 1;
    }
    tokens.push_back(input_str.substr(pos));
    return tokens;
}

// Estrutura de dados para os logs
#pragma pack(push, 1)
struct LogEntry {
    char sensor_id[32];
    std::time_t timestamp;
    double value;
};
#pragma pack(pop)

// Declarações de funções auxiliares
int save_log_entry(const std::string& sensor_id, const std::string& time_str, const std::string& value_str);
bool is_new_sensor(const std::string& sensor_id);
std::string fetch_log_entries(const std::string& sensor_id, int num_entries);

class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    explicit ClientSession(tcp::socket socket) : socket_(std::move(socket)) {}

    void start() {
        read_request();
    }

private:
    void read_request() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, buffer_, "\r\n",
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    std::istream stream(&buffer_);
                    std::string request(std::istreambuf_iterator<char>(stream), {});
                    auto parts = tokenize_string(request, '|');
                    std::string message_type = parts[0];
                    std::string sensor_id = parts[1];

                    if (message_type == "LOG") {
                        std::string timestamp = parts[2];
                        std::string value = parts[3];
                        save_log_entry(sensor_id, timestamp, value); // Salva no arquivo correspondente ao sensor
                        send_response("OK"); // Resposta de confirmação
                    } else if (message_type == "GET") {
                        if (!is_new_sensor(sensor_id)) {
                            int records_to_fetch = std::stoi(parts[2]);
                            std::string records = fetch_log_entries(sensor_id, records_to_fetch);
                            send_response(records);
                        } else {
                            std::string error_message = "ERROR|INVALID_SENSOR_" + sensor_id + "\r\n";
                            send_response(error_message);
                        }
                    }
                }
            });
    }

    void send_response(const std::string& message) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(message),
            [this, self, message](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    read_request(); // Continuar lendo a próxima requisição
                }
            });
    }

    tcp::socket socket_;
    boost::asio::streambuf buffer_;
};

class LogServer {
public:
    LogServer(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        accept_connection();
    }

private:
    void accept_connection() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<ClientSession>(std::move(socket))->start();
                }
                accept_connection();
            });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: log_server <port>\n";
        return 1;
    }

    boost::asio::io_context io_context;
    LogServer server(io_context, std::atoi(argv[1]));
    io_context.run();
    return 0;
}

/// Funções auxiliares

bool is_new_sensor(const std::string& sensor_id) {
    std::ifstream file(sensor_id + ".dat", std::ios::binary);
    return !file.is_open();
}

int save_log_entry(const std::string& sensor_id, const std::string& time_str, const std::string& value_str) {
    std::fstream file(sensor_id + ".dat", std::fstream::out | std::fstream::in | std::fstream::binary | std::fstream::app);
    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        int file_size = file.tellg();
        int num_records = file_size / sizeof(LogEntry);

        LogEntry entry;
        std::strcpy(entry.sensor_id, sensor_id.c_str());
        entry.timestamp = convert_string_to_time(time_str);
        entry.value = std::stod(value_str);

        file.write(reinterpret_cast<char*>(&entry), sizeof(LogEntry));

        std::cout << "Saved: " << entry.sensor_id << " | " << convert_time_to_string(entry.timestamp) << " | " << entry.value << "\n";
        file.close();
    } else {
        std::cerr << "Error opening file!\n";
    }
    return 0;
}

std::string fetch_log_entries(const std::string& sensor_id, int num_entries) {
    std::fstream file(sensor_id + ".dat", std::fstream::in | std::fstream::binary);
    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        int file_size = file.tellg();
        int total_records = file_size / sizeof(LogEntry);

        if (num_entries > total_records) {
            num_entries = total_records;
        }

        file.seekg((total_records - num_entries) * sizeof(LogEntry), std::ios::beg);

        LogEntry entry;
        std::string result = std::to_string(num_entries);

        for (int i = 0; i < num_entries; ++i) {
            file.read(reinterpret_cast<char*>(&entry), sizeof(LogEntry));
            result += ";" + convert_time_to_string(entry.timestamp) + "|" + std::to_string(entry.value);
        }

        result += "\r\n";
        file.close();
        return result;
    } else {
        std::cerr << "Error opening file!\n";
        return "ERROR";
    }
}
