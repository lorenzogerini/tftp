# Trivial File Transfer Protocol (TFTP)

This is a simple implementation of the Trivial File Transfer Protocol (TFTP) in C, as described in [RFC1350](https://www.rfc-editor.org/rfc/rfc1350).

With respect to the original TFTP, the following assumptions are made

1. Only Server-to-Client transfer is implemented (Read and Download), while Client-to-Server transfer (Write) is not implemented.

2. It is assumed that messages can not get lost (ritrasmission was not implemented).

3. Only the following errors are handled:
		a. File not found
		b. Illegal TFTP operation


## Usage

1. Compile:
		`make`

2. Start server
	`./tftp_server <port> <directory_files>`

3. Start client
	`./tftp_client <IP_server> <server_port>`


Useful commands:

- `!help`: shows all available commands
- `!mode {txt|bin}`: let the user specify the file transfer mode (bin or txt)
- `!get filename local_name`: client requests to the server the transfer of the file <filename>, which is saved locally to the path <local_name>.  
- `!quit`
