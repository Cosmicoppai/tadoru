#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/errqueue.h>
#include <netinet/ip_icmp.h>


int get_udp_socket() {
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		printf("failed to create socket: %d\n", fd);
		return -1;
	}
	return fd;
}

int get_udp_address(char *server_name, char server_port[], struct addrinfo **server_addr_info) {

	struct addrinfo hint = {0};

        hint.ai_family = AF_INET;
        hint.ai_socktype = SOCK_DGRAM;

        int status = getaddrinfo(server_name, server_port, &hint, server_addr_info);

        if (status != 0) {
                printf("getaddrinfo failed: %d\n", status);
                return status;
        }
	return status;

}

int main(int argc, char *argv[]) {
	if ( argc != 2) {
		printf("Need server address\n");
		return 1;
	}

	char *server_name = argv[1];
	char server_port[] = "50000";
	struct addrinfo *server_addr_info;

	int status = get_udp_address(server_name, server_port, &server_addr_info);

	if ( status != 0) {
		return 1;
	};


	int socket_fd = get_udp_socket();

	char recv_buffer[1024];
	const char *msg = "辿";

	// Each packet size will be: ipv4 header (20 bytes) + udp header(8 bytes) + msg (3 bytes) = Total(31 bytes)

	fd_set readfds;
	struct timeval tv;

	// TODO: allow dynamic size packets later
	printf("%s を辿る, 32 byte packets\n", server_name);

	for (int hop = 1; hop < 31; hop++) { // staring from 1 as ttl 0 not valid
		int status_code = setsockopt(socket_fd, IPPROTO_IP, IP_TTL, &hop, sizeof(hop));
		if ( status_code !=0) {
			 perror("setsockopt");
			return 1;
		}

		int on = 1;
		status_code = setsockopt(socket_fd, IPPROTO_IP, IP_RECVERR, &on, sizeof(on));
		if ( status_code !=0) {
			perror("setsockopt");
			return 1;
		}

		FD_ZERO(&readfds);
		FD_SET(socket_fd, &readfds);

		tv.tv_sec = 3;
		tv.tv_usec = 0;

		sendto(socket_fd, msg, strlen(msg), 0, server_addr_info->ai_addr, server_addr_info->ai_addrlen);

		int ret = select(socket_fd+1, &readfds, NULL, NULL, &tv);

		if ( ret > 0 && FD_ISSET(socket_fd, &readfds) ) {
			
			struct msghdr msg = {0};
			struct iovec iov;
			struct sockaddr_in sender_addr; // for remote addr
			
			iov.iov_base = recv_buffer;  // to store the data
			iov.iov_len = sizeof(recv_buffer) - 1;

			msg.msg_name = &sender_addr;  // to store the ip and other info of the sender
			msg.msg_namelen = sizeof(sender_addr);
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;

			char control_buffer[512];  // to store the errors
			msg.msg_control = control_buffer;
			msg.msg_controllen = sizeof(control_buffer);

			ssize_t bytes_read = recvmsg(socket_fd, &msg, MSG_ERRQUEUE);
			if (bytes_read < 0) {
				perror("recvmsg");
			}
			struct cmsghdr *cmsg;
				for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {

					// checking message level is ipv4 and
					if ( cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVERR) {
						struct sock_extended_err *err_msg = (struct sock_extended_err *)CMSG_DATA(cmsg);
						if ( err_msg->ee_origin == SO_EE_ORIGIN_ICMP ) {
							struct sockaddr_in *sin = (struct sockaddr_in *)(err_msg+1);
							char ip_str[INET_ADDRSTRLEN];
							inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
							// https://www.firewall.cx/networking/network-protocols/icmp-protocol/icmp-time-exceeded.html
							if ( err_msg->ee_type == ICMP_TIME_EXCEEDED) {
								printf("%d: %s\n", hop, ip_str);
							} else if ( err_msg->ee_type == ICMP_UNREACH) {
								printf("%d: %s\n", hop, ip_str);
								break;
							}
						}
					}
				}
		} else {
			printf("%d: * * *\n", hop);
		}
	}
	close(socket_fd);
	freeaddrinfo(server_addr_info);


}
