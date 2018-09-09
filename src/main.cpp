#include "scnet.hpp"

#include <iostream>

struct program_args {
	uint16_t port;
	size_t num_threads;
};

static void print_usage() {
	std::cout << "epoll-echo (c) Antoine Henry 2018" << std::endl;
	std::cout << "  usage: epoll-echo -p <PORT> -t <NUMBER OF THREADS>" << std::endl;
}

static bool read_args(int argc, char** argv, program_args& args) {
	args.port = 1060;
	args.num_threads = 2;

	// No arguments given
	if (argc == 1) {
		std::cout << "epoll-echo: This program requires arguments to run. Run with -h for help.\n";
	}

	for (size_t i = 1; i < (size_t)argc; i++) {
		if (argv[i] == std::string("-h")) { // Help
			print_usage();
			return false;
		}
		else if (argv[i] == std::string("-p")) { // Port
			if ((i + 1) < (size_t)argc)
				args.port = std::atoi(argv[++i]);
		}
		else if (argv[i] == std::string("-t")) { // Port
			if ((i + 1) < (size_t)argc)
				args.num_threads = std::atoi(argv[++i]);
		}
		else { // Default
			std::cout << "epoll-echo: Invalid command." << std::endl;
			print_usage();
			return false;
		}
	}
	return true;
}

class device_callbacks : public scnet::device_callbacks {
public:
	~device_callbacks() {

	}

	bool on_startup(scnet::device& device) {
		(void)device;
		std::cout << "Epoll Echo Server started up.\n";
		return true;
	}

	void on_shutdown(scnet::device& device) {
		(void)device;
		std::cout << "Epoll Echo Server shut down.\n";
	}

	bool on_accept(scnet::device& device, scnet::client_state* client) {
		scnet::address address;

		device.get_client_address(client, address);
		std::cout << "Client accepted from " <<
		(int)address.ip[0] << "." <<
		(int)address.ip[1] << "." <<
		(int)address.ip[2] << "." <<
		(int)address.ip[3] << "." << std::endl;

		return true;
	}

	bool on_disconnect(scnet::device& device, scnet::client_state* client) {
		scnet::address address;
		
		device.get_client_address(client, address);
		std::cout << "Client from " <<
		(int)address.ip[0] << "." <<
		(int)address.ip[1] << "." <<
		(int)address.ip[2] << "." <<
		(int)address.ip[3] << " disconnected." << std::endl;
		return true;
	}

	int64_t on_read(scnet::device& device, scnet::client_state* client, const void* buffer, size_t size) {
		std::string message((char*)buffer, size);
		std::string response = "[ECHO]: ";
		std::cout << "Message recieved: " << message << std::endl;

		device.write_buffer(client, response.data(), response.size() + 1);
		device.write_buffer(client, message.data(), message.size() + 1);

		return size;
	}
};

int main(int argc, char** argv) {
	program_args args = {};
	if (!read_args(argc, argv, args)) {
		return -1;
	}

	std::unique_ptr<device_callbacks> callbacks(new device_callbacks);
	scnet::device device(std::move(callbacks), args.port, 16, args.num_threads);

	for(;;);

	return 0;
}