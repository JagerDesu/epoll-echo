#include "scnet.hpp"

#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netdb.h>

#include <iostream>
#include <algorithm>
#include <cstring>

namespace scnet {


device_callbacks::~device_callbacks() {

}

static bool set_fd_blocking(int fd, bool blocking) {
	if (fd < 0)
		return false;

	int flags = fcntl(fd, F_GETFL, 0);

	if (flags == -1)
		return false;

	if (blocking)
		flags = (flags & ~O_NONBLOCK);
	else
		flags = (flags | O_NONBLOCK);
	return (fcntl(fd, F_SETFL, flags) == 0);
}

static void sockaddr_in_to_address(const sockaddr_in& in_addr, address& out_addr) {
	out_addr.port = ntohs(in_addr.sin_port);
	memcpy(out_addr.ip, &in_addr.sin_addr.s_addr, sizeof(uint32_t));
}

// Creates a nonblocking socket
static int create_socket() {
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	set_fd_blocking(fd, false);
	return fd;
}

struct client_state {
	struct address address;
	void* userdata;
	int fd;
	std::vector<uint8_t> read_buffer;
	uint64_t bytes_read;
	uint64_t bytes_written;
};

device::device(std::unique_ptr<device_callbacks> callbacks, uint16_t port, size_t client_hint, size_t num_threads) :
	callbacks(std::move(callbacks))
{
	struct sockaddr_in server_addr = {};
	int result;

	epoll_fd = epoll_create(client_hint);

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	listener = allocate_client();

	listener->fd = create_socket();

	// Bind the listener to the address.
	result = bind(
		listener->fd,
		reinterpret_cast<struct sockaddr*>(&server_addr),
		sizeof(struct sockaddr_in)
	);

	if (result != 0) {
		std::cout << "Cannot bind listener on port " << port << "." << std::endl;
		abort();
	}

	result = listen(listener->fd, 256);
	if (result != 0) {
		std::cout << "Cannot listen $on port " << port << "." << std::endl;
		abort();
	}

	threads.reserve(num_threads);

	for (size_t i = 0; i < num_threads; i++) {
		// Threads try to copy function arguments; explicitly pass a reference.
		threads.push_back(std::thread(worker, std::ref(*this), i));
	}

	struct epoll_event event = {};

	event.data.ptr = listener;
	event.events = EPOLLIN | EPOLLET;
	result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener->fd, &event);
	if (result == -1) {
		perror("epoll_ctl");
		abort();
	}

	if (this->callbacks) {
		this->callbacks->on_startup(*this);
	}
}

device::~device() {
	if (callbacks)
		callbacks->on_shutdown(*this);

	disconnect_client(listener);

	for (auto& thread : threads) {
		thread.join();
	}

	close(epoll_fd);
}

void device::disconnect_client(client_state* client) {
	if (!client)
		return;
	if (callbacks)
		callbacks->on_disconnect(*this, client);
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, nullptr);
	shutdown(client->fd, SHUT_RDWR);
	close(client->fd);
	deallocate_client(client);
}

void device::set_client_userdata(client_state* client, void* userdata) {
	client->userdata = userdata;
}

void* device::get_client_userdata(client_state* client) const {
	void* userdata = client->userdata;
	return userdata;
}

void device::get_client_address(client_state* client, address& value) const {
	value = client->address;
}

int64_t device::write_buffer(client_state* client, const void* buffer, size_t size) {
	int fd = client->fd;
	ssize_t result = send(fd, buffer, size, 0);

	if (result > 0) {
		client->bytes_written += result;
	}
	return result;
}

void device::handle_accept() {
	for(;;) {
		struct sockaddr_in input_addr = {};
		socklen_t output_addr_size = 0;
		int in_fd = -1;
		int result = -1;

		output_addr_size = sizeof(input_addr);

		in_fd = accept(
			listener->fd,
			reinterpret_cast<struct sockaddr*>(&input_addr),
			&output_addr_size
		);

		if (in_fd == -1) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				/* We have processed all incoming
				connections. */
				break;
			}

			else {
				perror ("accept");
				break;
			}
		}

		set_fd_blocking(in_fd, false);

		// Extract the client address
		address address = {};

		sockaddr_in_to_address(input_addr, address);

		// Allocate a client and register them with epoll
		auto new_client = allocate_client();
		struct epoll_event new_event = {};

		new_client->address = address;
		new_client->fd = in_fd;
		new_event.data.ptr = new_client;
		new_event.events = EPOLLIN | EPOLLET;

		if (callbacks)
			callbacks->on_accept(*this, new_client);

		result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, in_fd, &new_event);
		if(result == -1) {
			perror ("epoll_ctl");
			abort ();
		}
	}
}

void device::handle_read(client_state* client) {
	/*-
	 * We have data on the fd waiting to be read. Read and
	 * display it. We must read whatever data is available
	 * completely, as we are running in edge-triggered mode
	 * and won't get a notification again for the same
	 * data.
	 */
	for(;;) {
		std::array<char, 512> buffer;
		ssize_t count;

		count = read(client->fd, buffer.data(), buffer.size());
		if (count == -1) {
			/* If errno == EAGAIN, that means we have read all data. So go back to the main loop. */
			if (errno != EAGAIN) {
				perror("read");
			}
			break;
		}
		else if (count == 0) {
			// Client disconnected
			disconnect_client(client);
			break;
		}

		client->bytes_read += count;

		if (callbacks && (count > 0)) {
			auto& rb = client->read_buffer;
			// Add the read data to the client buffer
			rb.insert(rb.end(), buffer.begin(), buffer.begin() + count);
			
			count = callbacks->on_read(
				*this,
				client,
				rb.data(),
				rb.size()
			);

			rb = std::vector<uint8_t>(rb.begin() + count, rb.end());
		}
	}
}

// Should be replaced with an object pool
client_state* device::allocate_client() {
	return new client_state;
}

void device::deallocate_client(client_state* client) {
	delete client;
}

void device::worker(device& device, size_t id) {
	std::cout << "Worker spawned on thread \'" << id << "\'" << std::endl;
	const size_t MAX_EVENTS = 64;

	for (;;) {
		std::array<struct epoll_event, MAX_EVENTS> events;
		// Check if the kernel has any events waiting for us.
		int num_events = epoll_wait(device.epoll_fd, events.data(), events.size(), -1);

		if (num_events == -1) {
			perror("epoll_wait");
			continue;
		}

		// Loop through the kernel's events.
		for (size_t i = 0; i < (size_t)num_events; i++) {
			auto& event = events[i];
			auto client = static_cast<client_state*>(event.data.ptr);

			if (client) {
				if (client->fd == device.listener->fd)
					device.handle_accept();
				else
					device.handle_read(client);
			}

			else {
				break;
			}
		}
	}
}
}