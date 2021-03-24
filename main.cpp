#include <winsock2.h>
#include <Ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32")

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cfloat>
using namespace std;


SOCKET udp_socket = INVALID_SOCKET;
enum program_mode { send_mode, receive_mode };


void print_usage(void)
{
	cout << "  USAGE:" << endl;
	cout << "   Receive mode:" << endl;
	cout << "    udpspeed PORT_NUMBER" << endl;
	cout << endl;
	cout << "   Send mode:" << endl;
	cout << "    udpspeed TARGET_HOST PORT_NUMBER" << endl;
	cout << endl;
	cout << "   ie:" << endl;
	cout << "    Receive mode: udpspeed 1920" << endl;
	cout << "    Send mode:    udpspeed www 342" << endl;
	cout << "    Send mode:    udpspeed 127.0.0.1 950" << endl;
	cout << endl;
}

bool verify_port(const string& port_string, unsigned long int& port_number)
{
	for (size_t i = 0; i < port_string.length(); i++)
	{
		if (!isdigit(port_string[i]))
		{
			cout << "  Invalid port: " << port_string << endl;
			cout << "  Ports are specified by numerals only." << endl;
			return false;
		}
	}

	istringstream iss(port_string);
	iss >> port_number;

	if (port_string.length() > 5 || port_number > 65535 || port_number == 0)
	{
		cout << "  Invalid port: " << port_string << endl;
		cout << "  Port must be in the range of 1-65535" << endl;
		return false;
	}

	return true;
}

bool init_winsock(void)
{
	WSADATA wsa_data;
	WORD ver_requested = MAKEWORD(2, 2);

	if (WSAStartup(ver_requested, &wsa_data))
	{
		cout << "Could not initialize Winsock 2.2.";
		return false;
	}

	if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2)
	{
		cout << "Required version of Winsock (2.2) not available.";
		return false;
	}

	return true;
}

bool init_options(const int& argc, char** argv, enum program_mode& mode, string& target_host_string, long unsigned int& port_number)
{
	if (!init_winsock())
		return false;

	string port_string = "";

	if (2 == argc)
	{
		mode = receive_mode;
		port_string = argv[1];
	}
	else if (3 == argc)
	{
		mode = send_mode;
		target_host_string = argv[1];
		port_string = argv[2];
	}
	else
	{
		print_usage();
		return false;
	}

	return verify_port(port_string, port_number);
}

void cleanup(void)
{
	// if the socket is still open, close it
	if (INVALID_SOCKET != udp_socket)
		closesocket(udp_socket);

	// shut down winsock
	WSACleanup();
}

class thread_loads
{
public:

	vector<double> loads;
	size_t thread_id;

	double total(void) const
	{
		double t = 0;

		for (size_t i = 0; i < loads.size(); i++)
			t += loads[i];

		return t;
	}

	bool operator<(const thread_loads& rhs)
	{
		if (total() < rhs.total())
			return true;

		return false;
	}
};

class IPv4_address
{
public:
	unsigned char byte0, byte1, byte2, byte3;

	inline bool operator<(const IPv4_address& right) const
	{
		if (right.byte0 > byte0)
			return true;
		else if (right.byte0 < byte0)
			return false;

		if (right.byte1 > byte1)
			return true;
		else if (right.byte1 < byte1)
			return false;

		if (right.byte2 > byte2)
			return true;
		else if (right.byte2 < byte2)
			return false;

		if (right.byte3 > byte3)
			return true;
		else if (right.byte3 < byte3)
			return false;

		return false;
	}

	string get_string(void)
	{
		ostringstream oss;

		oss << static_cast<int>(byte0) << ".";
		oss << static_cast<int>(byte1) << ".";
		oss << static_cast<int>(byte2) << ".";
		oss << static_cast<int>(byte3);

		return oss.str();
	}
};

class stats
{
public:

	IPv4_address ip_addr;

	long long unsigned int total_elapsed_ticks = 0;
	long long unsigned int total_bytes_received = 0;
	long long unsigned int last_reported_at_ticks = 0;
	long long unsigned int last_reported_total_bytes_received = 0;

	long long unsigned int last_nonzero_update = 0;

	double record_bps = 0.0;
	double bytes_per_second = 0.0;
};

class packet
{
public:

	vector<char> packet_buf;
	IPv4_address ip_addr;
};


void thread_func(atomic_bool& stop, atomic_bool& thread_done, map<IPv4_address, stats>& jobstats, vector<packet>& packets, mutex& m, vector<string>& return_data)
{
	thread_done = false;

	while (!stop)
	{
		m.lock();

		if (packets.size() > 0)
		{
			for (size_t i = 0; i < packets.size(); i++)
			{
				if (jobstats.find(packets[i].ip_addr) == jobstats.end())
				{
					return_data.push_back("Mismanaged packet");
					continue;
				}

				// Do stuff with packet buffer here
				jobstats[packets[i].ip_addr].total_bytes_received += packets[i].packet_buf.size();
				// Do stuff with packet buffer here
			}

			packets.clear();
		}

		m.unlock();
	}

	thread_done = true;
}

class job_handler
{
public:

	thread t;
	atomic_bool stop = false;
	atomic_bool thread_done = false;
	mutex m;

	map<IPv4_address, stats> jobstats;
	vector<packet> packets;
	vector<string> log;

	job_handler(void)
	{
		t = thread(thread_func, ref(stop), ref(thread_done), ref(jobstats), ref(packets), ref(m), ref(log));
	}

	~job_handler(void)
	{
		stop = true;

		while (false == thread_done)
		{
			// cout << "Waiting for thread to return" << endl;
		}

		t.join();
	}
};

double standard_deviation(const vector<double>& src)
{
	double mean = 0;
	double size = static_cast<double>(src.size());

	for (size_t i = 0; i < src.size(); i++)
		mean += src[i];

	mean /= size;

	double sq_diff = 0;

	for (size_t i = 0; i < src.size(); i++)
	{
		double diff = src[i] - mean;
		sq_diff += diff * diff;
	}

	sq_diff /= size;

	return sqrt(sq_diff);
}


int main(int argc, char** argv)
{
	cout << endl << "udpspeed_5 1.0 - UDP speed tester" << endl << "Copyright 2021, Shawn Halayka" << endl << endl;

	program_mode mode = receive_mode;

	string target_host_string = "";
	long unsigned int port_number = 0;

	const long unsigned int tx_buf_size = 65507;
	vector<char> tx_buf(tx_buf_size, 0);

	const long unsigned int rx_buf_size = 65507;
	vector<char> rx_buf(rx_buf_size, 0);

	if (!init_options(argc, argv, mode, target_host_string, port_number))
	{
		cleanup();
		return 1;
	}

	if (send_mode == mode)
	{
		cout << "  Sending on port " << port_number << " - CTRL+C to exit." << endl;

		struct addrinfo hints;
		struct addrinfo* result;

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = 0;
		hints.ai_protocol = IPPROTO_UDP;

		ostringstream oss;
		oss << port_number;

		if (0 != getaddrinfo(target_host_string.c_str(), oss.str().c_str(), &hints, &result))
		{
			cout << "  getaddrinfo error." << endl;
			freeaddrinfo(result);
			cleanup();
			return 2;
		}

		if (INVALID_SOCKET == (udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)))
		{
			cout << "  Could not allocate a new socket." << endl;
			freeaddrinfo(result);
			cleanup();
			return 3;
		}

		while (1)
		{
			if (SOCKET_ERROR == (sendto(udp_socket, &tx_buf[0], tx_buf_size, 0, result->ai_addr, sizeof(struct sockaddr))))
			{
				cout << "  Socket sendto error." << endl;
				freeaddrinfo(result);
				cleanup();
				return 4;
			}
		}

		freeaddrinfo(result);
	}
	else if (receive_mode == mode)
	{
		cout << "  Thread count: " << std::thread::hardware_concurrency() << endl;
		cout << "  Receiving on UDP port " << port_number << " - CTRL+C to exit." << endl;

		std::chrono::high_resolution_clock::time_point update_start_time = std::chrono::high_resolution_clock::now();

		struct sockaddr_in my_addr;
		struct sockaddr_in their_addr;
		int addr_len = 0;

		my_addr.sin_family = AF_INET;
		my_addr.sin_port = htons(static_cast<unsigned short int>(port_number));
		my_addr.sin_addr.s_addr = INADDR_ANY;
		memset(&(my_addr.sin_zero), '\0', 8);
		addr_len = sizeof(struct sockaddr);

		if (INVALID_SOCKET == (udp_socket = socket(AF_INET, SOCK_DGRAM, 0)))
		{
			cout << "  Could not allocate a new socket." << endl;
			cleanup();
			return 5;
		}

		if (SOCKET_ERROR == bind(udp_socket, reinterpret_cast<struct sockaddr*>(&my_addr), sizeof(struct sockaddr)))
		{
			cout << "  Could not bind socket to port " << port_number << "." << endl;
			cleanup();
			return 6;
		}

		size_t num_threads = std::thread::hardware_concurrency();

		vector<job_handler> handlers(num_threads);
		map<IPv4_address, size_t> ip_to_thread_map;

		const double mbits_factor = 8.0 / (1024.0 * 1024.0);
		const long long unsigned int ticks_per_second = 1000000000;

		srand(0);

		while (1)
		{
			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 100000; // one hundred thousand microseconds is one-tenth of a second

			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(udp_socket, &fds);

			int select_ret = select(0, &fds, 0, 0, &timeout);

			if (SOCKET_ERROR == select_ret)
			{
				cout << "  Socket select error." << endl;
				cleanup();
				return 7;
			}
			else if (0 < select_ret)
			{
				int temp_bytes_received = 0;

				if (SOCKET_ERROR == (temp_bytes_received = recvfrom(udp_socket, &rx_buf[0], rx_buf_size, 0, reinterpret_cast<struct sockaddr*>(&their_addr), &addr_len)))
				{
					cout << "  Socket recvfrom error." << endl;
					cleanup();
					return 8;
				}

				IPv4_address client_address;
				client_address.byte0 = their_addr.sin_addr.S_un.S_un_b.s_b1;
				client_address.byte1 = their_addr.sin_addr.S_un.S_un_b.s_b2;
				client_address.byte2 = their_addr.sin_addr.S_un.S_un_b.s_b3;
				client_address.byte3 = their_addr.sin_addr.S_un.S_un_b.s_b4;

				// Use a pseudorandom IP to emulate many clients
				// This is to be used only for testing purposes
				//client_address.byte0 = 127;
				//client_address.byte1 = 0;
				//client_address.byte2 = 0;
				//client_address.byte3 = rand()%256;

				size_t thread_index = 0;

				if (ip_to_thread_map.find(client_address) == ip_to_thread_map.end())
				{
					thread_index = rand() % num_threads;

					ip_to_thread_map[client_address] = thread_index;

					handlers[thread_index].m.lock();
					handlers[thread_index].jobstats[client_address].ip_addr = client_address;
					handlers[thread_index].m.unlock();
				}
				else
				{
					thread_index = ip_to_thread_map[client_address];
				}

				packet p;
				p.packet_buf = rx_buf;
				p.packet_buf.resize(temp_bytes_received);
				p.ip_addr = client_address;

				handlers[thread_index].m.lock();
				handlers[thread_index].packets.push_back(p);
				handlers[thread_index].m.unlock();
			}

			const std::chrono::high_resolution_clock::time_point update_end_time = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double, std::nano> update_elapsed = update_end_time - update_start_time;

			// Update data and do load balancing once per second
			if (update_elapsed.count() >= ticks_per_second)
			{
				for (size_t t = 0; t < num_threads; t++)
					handlers[t].m.lock();

				// Update data
				for (size_t t = 0; t < num_threads; t++)
				{
					double per_thread_total_bps = 0;

					for (map<IPv4_address, stats>::iterator i = handlers[t].jobstats.begin(); i != handlers[t].jobstats.end();)
					{
						i->second.total_elapsed_ticks += static_cast<unsigned long long int>(update_elapsed.count());

						const long long unsigned int actual_ticks = i->second.total_elapsed_ticks - i->second.last_reported_at_ticks;
						const long long unsigned int bytes_sent_received_between_reports = i->second.total_bytes_received - i->second.last_reported_total_bytes_received;
						i->second.bytes_per_second = static_cast<double>(bytes_sent_received_between_reports) / (static_cast<double>(actual_ticks) / static_cast<double>(ticks_per_second));

						if (i->second.bytes_per_second > i->second.record_bps)
							i->second.record_bps = i->second.bytes_per_second;

						i->second.last_reported_at_ticks = i->second.total_elapsed_ticks;
						i->second.last_reported_total_bytes_received = i->second.total_bytes_received;

						per_thread_total_bps += i->second.bytes_per_second;

						if (i->second.bytes_per_second != 0)
						{
							i->second.last_nonzero_update = i->second.total_elapsed_ticks;
							i++;
						}
						else
						{
							// kill job
							if ((i->second.total_elapsed_ticks - i->second.last_nonzero_update) > (ticks_per_second * 10))
							{
								ip_to_thread_map.erase(ip_to_thread_map.find(i->second.ip_addr));
								i = handlers[t].jobstats.erase(i);
							}
							else
							{
								i++;
							}
						}
					}

					cout << "Thread " << t << ' ' << per_thread_total_bps * mbits_factor << " Mbit/s" << endl;
				}

				// Do load balancing
				while(1)
				{
					// Get pre-move mean and standard deviation
					double average = 0;
					vector<double> bps;

					for (size_t t = 0; t < num_threads; t++)
					{
						double per_thread_total_bps = 0;

						for (map<IPv4_address, stats>::iterator i = handlers[t].jobstats.begin(); i != handlers[t].jobstats.end(); i++)
							per_thread_total_bps += i->second.bytes_per_second;

						per_thread_total_bps *= mbits_factor;

						bps.push_back(per_thread_total_bps);

						average += per_thread_total_bps;
					}

					average /= num_threads;

					cout << "Temporary mean: " << average << " +/- " << standard_deviation(bps) << endl;

					// Enumerate thread loads
					vector<thread_loads> thread_loads_vec;

					for (size_t t = 0; t < num_threads; t++)
					{
						thread_loads tl;

						tl.thread_id = t;

						for (map<IPv4_address, stats>::const_iterator ci = handlers[t].jobstats.begin(); ci != handlers[t].jobstats.end(); ci++)
							tl.loads.push_back(ci->second.bytes_per_second);

						thread_loads_vec.push_back(tl);
					}
					
					// Sort in reverse order by total
					sort(thread_loads_vec.rbegin(), thread_loads_vec.rend());

					// Find candidate thread
					bool found_candidate = false;
					size_t candidate_thread_id = 0;

					for (size_t i = 0; i < thread_loads_vec.size(); i++)
					{
						if (handlers[thread_loads_vec[i].thread_id].jobstats.size() > 1)
						{
							// This will still happen on a dead IP until that IP is timed out for 10 seconds and erased
							found_candidate = true;
							candidate_thread_id = thread_loads_vec[i].thread_id;
							break;
						}
					}

					// If no threads had more than 1 job, then abort
					if (false == found_candidate)
						break;

					// If the candidate thread is the last element in the vector, then abort
					if (candidate_thread_id == thread_loads_vec[thread_loads_vec.size() - 1].thread_id)
						break;

					// Jobs are not sorted, so just grab an iterator to the first element
					// This is as good as picking an iterator randomly from sorted jobs
					map<IPv4_address, stats>::const_iterator ci = handlers[candidate_thread_id].jobstats.begin();
					
					// Back up and add job
					stats old_dest_stats = ci->second;
					handlers[thread_loads_vec[thread_loads_vec.size() - 1].thread_id].jobstats.insert(*ci);

					// Back up and assign IP
					size_t old_thread_id = ip_to_thread_map[ci->second.ip_addr];
					IPv4_address ip_address = ci->second.ip_addr;
					ip_to_thread_map[ci->second.ip_addr] = thread_loads_vec[thread_loads_vec.size() - 1].thread_id;

					// Erase job
					handlers[candidate_thread_id].jobstats.erase(ci);

					// Get post-move mean and standard deviation
					double pre_std_dev = standard_deviation(bps);

					average = 0;
					bps.clear();

					for (size_t t = 0; t < num_threads; t++)
					{
						double per_thread_total_bps = 0;

						for (map<IPv4_address, stats>::iterator i = handlers[t].jobstats.begin(); i != handlers[t].jobstats.end(); i++)
							per_thread_total_bps += i->second.bytes_per_second;

						per_thread_total_bps *= mbits_factor;

						bps.push_back(per_thread_total_bps);

						average += per_thread_total_bps;
					}

					average /= num_threads;

					// Found (local) minimum
					if (standard_deviation(bps) >= pre_std_dev)
					{
						// Roll back changes
						handlers[candidate_thread_id].jobstats.insert(std::pair<IPv4_address, stats>(ip_address, old_dest_stats));

						ip_to_thread_map[ip_address] = old_thread_id;

						handlers[thread_loads_vec[thread_loads_vec.size() - 1].thread_id].jobstats.erase(
							handlers[thread_loads_vec[thread_loads_vec.size() - 1].thread_id].jobstats.find(ip_address)
						);

						// Get final mean and standard deviation
						average = 0;
						bps.clear();

						for (size_t t = 0; t < num_threads; t++)
						{
							double per_thread_total_bps = 0;

							for (map<IPv4_address, stats>::iterator i = handlers[t].jobstats.begin(); i != handlers[t].jobstats.end(); i++)
								per_thread_total_bps += i->second.bytes_per_second;

							per_thread_total_bps *= mbits_factor;

							bps.push_back(per_thread_total_bps);

							average += per_thread_total_bps;
						}

						average /= num_threads;

						cout << "Final mean: " << average << " +/- " << standard_deviation(bps) << endl << endl;

						break;
					}
				}

				// Set up timer for next update
				update_start_time = update_end_time;

				for (size_t t = 0; t < num_threads; t++)
					handlers[t].m.unlock();
			}
		}
	}

	cleanup();

	return 0;
}
