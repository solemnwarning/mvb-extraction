#include <assert.h>
#include <winsock2.h>
#include <windows.h>

#include "DummyPrinter.hpp"

DummyPrinter::DummyPrinter(unsigned short port):
	running(true)
{
	WSADATA wsdata;
	int status = WSAStartup(MAKEWORD(2, 0), &wsdata);
	if(status != 0)
	{
		abort();
	}

	server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(server_socket == SOCKET_ERROR)
	{
		abort();
	}

	struct sockaddr_in bind_addr;
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bind_addr.sin_port = htons(port);

	if(bind(server_socket, (struct sockaddr*)(&bind_addr), sizeof(bind_addr)) != 0)
	{
		abort();
	}

	if(listen(server_socket, 10) != 0)
	{
		abort();
	}

	InitializeCriticalSection(&lock);

	print_job_completed_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	assert(print_job_completed_event != NULL);

	DWORD thread_id;
	print_server_thread = CreateThread(NULL, 0, &print_server_main, this, 0, &thread_id);
	if(print_server_thread == NULL)
	{
		printf("CreateThread: %u\n", (unsigned)(GetLastError()));
		abort();
	}
}

DummyPrinter::~DummyPrinter()
{
	running = false;

	WaitForSingleObject(print_server_thread, INFINITE);
	CloseHandle(print_server_thread);

	CloseHandle(print_job_completed_event);

	closesocket(server_socket);
}

DWORD WINAPI DummyPrinter::print_server_main(LPVOID context)
{
	DummyPrinter *self = (DummyPrinter*)(context);

	std::vector<unsigned char> recv_buf(1024);

	while(self->running)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);

		FD_SET(self->server_socket, &read_fds);
		int maxfd = self->server_socket;

		pj_iterator pj;
		for(pj = self->pending_jobs.begin(); pj != self->pending_jobs.end(); ++pj)
		{
			FD_SET(pj->first, &read_fds);
			
			if(pj->first > maxfd)
			{
				maxfd = pj->first;
			}
		}

		struct timeval timeout = { 1, 0 };
		int s = select((maxfd + 1), &read_fds, NULL, NULL, &timeout);

		if(s > 0)
		{
			if(FD_ISSET(self->server_socket, &read_fds))
			{
				int client_sock = accept(self->server_socket, NULL, NULL);
				if(client_sock != SOCKET_ERROR)
				{
					self->pending_jobs.insert(std::make_pair(client_sock, std::vector<unsigned char>()));
				}
			}

			for(pj = self->pending_jobs.begin(); pj != self->pending_jobs.end();)
			{
				if(FD_ISSET(pj->first, &read_fds))
				{
					int r = recv(pj->first, (char*)(&(recv_buf[0])), recv_buf.size(), 0);
					if(r < 0)
					{
						/* Read error. Abort the connection. */
						closesocket(pj->first);
						pj = self->pending_jobs.erase(pj);
					}
					else if(r == 0)
					{
						/* Remote end closed down the connection. */

						EnterCriticalSection(&(self->lock));
						self->completed_jobs.push(pj->second);
						SetEvent(self->print_job_completed_event);
						LeaveCriticalSection(&(self->lock));

						closesocket(pj->first);
						pj = self->pending_jobs.erase(pj);
					}
					else{
						/* Got some data. */

						pj->second.insert(pj->second.end(), &(recv_buf[0]), &(recv_buf[0]) + r);
						++pj;
					}
				}
				else{
					++pj;
				}
			}
		}
	}

	return 0;
}

bool DummyPrinter::wait_for_print(unsigned int seconds)
{
	WaitForSingleObject(print_job_completed_event, seconds * 1000);

	EnterCriticalSection(&lock);
	bool ready = !completed_jobs.empty();
	LeaveCriticalSection(&lock);

	return ready;
}

std::vector<unsigned char> DummyPrinter::get_next_print()
{
	std::vector<unsigned char> data;

	EnterCriticalSection(&lock);

	assert(!completed_jobs.empty());

	data = completed_jobs.front();
	completed_jobs.pop();

	if(completed_jobs.empty())
	{
		ResetEvent(print_job_completed_event);
	}
	
	LeaveCriticalSection(&lock);

	return data;
}
