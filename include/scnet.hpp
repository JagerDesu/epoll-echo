#include <cstddef>
#include <cstdint>
#include <vector>
#include <thread>
#include <memory>

namespace scnet {

// Address structure capable of storing IPv4 and IPv6 values
struct address {
	uint8_t ip[16] = {};
	uint16_t port = 0;
};

struct client_state;
class device;

class device_callbacks {
public:
	/*-
	 * Called on device startup.
	 */
	virtual bool on_startup(device& device) = 0;

	/*-
	 * Called on device shutdown.
	 */
	virtual void on_shutdown(device& device) = 0;

	/*-
	* client_state client
	*    Client that was accepted.
	* 
	* Return true if client is successfully accepted.
	*/
	virtual bool on_accept(device& device, client_state* client) = 0;

	/*-
	* client_state client
	*    Client that disconnected.
	*/
	virtual bool on_disconnect(device& device, client_state* client) = 0;

	/*-
	* client_state client
	*   client that needs data read.
	* 
	* const void* buffer
	*    Pointer to an internal buffer which contains all client data until now.
	* 
	* size_t size
	*    Size of the buffer
	* 
	* Returns the amount of bytes that were successfully processed. All unprocessed bytes will
	* be passed again upon the next read event.
	*/
	virtual int64_t on_read(device& device, client_state* client, const void* buffer, size_t size) = 0;

	virtual ~device_callbacks() = 0;
};

struct client_state;

/*-
 * Represents a network device. Supports multithreading, and allows the registration of callbacks.
 */
class device {
public:
	device(std::unique_ptr<device_callbacks> callbacks, uint16_t port, size_t client_hint, size_t num_threads);
	~device();

	device(device&&) = default;

	void disconnect_client(client_state* client);

	void set_client_userdata(client_state* client, void* userdata);
	void* get_client_userdata(client_state* client) const;

	void get_client_address(client_state* client, address& value) const;

	int64_t write_buffer(client_state* client, const void* buffer, size_t size);

private:
	client_state* allocate_client();
	void deallocate_client(client_state* client_state);

	static void worker(device& device, size_t id);
	void handle_accept();
	void handle_read(client_state* client);

	int epoll_fd;

	// Create a "faux" client to store the listener fd so we're not comparing
	// pointers with file descriptors ('struct epoll_event' has a 'data' union)
	client_state* listener;

	std::unique_ptr<device_callbacks> callbacks;
	std::vector<std::thread> threads;
};

}