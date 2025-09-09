# Great Approximator

**Great Approximator** is a TCP-based client-server game written in C. Each client connects to the server and competes by approximating a hidden polynomial function. The goal is to minimize the error between the approximation and the actual function.

## Game Concept

- The server provides each client with a unique polynomial function `f(x) = a0 + a1*x + ... + aN*x^N`.
- Clients modify their approximation `f̂(x)` by sending commands that add values to integer points between `0` and `K`.
- The game ends after `M` valid PUT operations (shared among all clients).
- Final scores are based on the sum of squared differences plus penalties for invalid or premature actions.

## Features

- Asynchronous handling of multiple TCP clients using IPv4 and IPv6.
- Text-based communication protocol using standard sockets (no external network libraries).
- Two modes of client operation:
  - **Interactive mode**: reads PUT commands from stdin.
  - **Automatic mode**: sends PUT commands based on an internal strategy (`-a`).
- Custom protocol with precise error handling and diagnostics.
- Handles invalid input, client disconnects, and protocol violations.

## Building

This project uses `make`. Just run:

```bash
make
```
To clean all generated files:
```bash
make clean
```
## Usage
### Server
```bash
./approx-server -f coefficients.txt [-p port] [-k K] [-n N] [-m M]
```
- `-f` is mandatory and points to the file with COEFF lines.
Optional:
- `-p` server port (default: 0 → random)
- `-k` max point value (default: 100)
- `-n` polynomial degree (default: 4)
- `-m` number of total PUT operations (default: 131)
### Client
```bash
./approx-client -u playerID -s serverAddress -p port [-4 | -6] [-a]
```
- `-u` your player identifier (alphanumeric)
- `-s` server address (IP or hostname)
- `-p` port to connect to
- `-4` or `-6` to force IPv4 or IPv6
- `-a` enables automatic approximation strategy
If `-a` is not specified, the client reads PUT commands from standard input like this:
```bash
0 3.5
2 1.25
```
Each line represents a PUT: ```PUT $point $value```.

## Protocol Overview

- HELLO – client identifies itself.
- COEFF – server sends polynomial coefficients.
- PUT – client adds a value to an approximation point.
- STATE – server replies with current approximation.
- PENALTY / BAD_PUT – client mistake notifications.
- SCORING – final results with error values per player.

## Project Structure

- Makefile → Build instructions
- README.md → Project documentation
- approx-server.c → TCP server implementation
- approx-client.c → TCP client implementation
- client.h → Server-side structure for managing connected clients
- cb.c / cb.h → Circular buffer for managing incoming TCP message streams
- queue.c / queue.h → Priority queue (event queue) used for scheduling and managing message flow per client
- err.c / err.h → Error handling utilities (prints diagnostics, handles fatal errors)
- common.c / common.h → Parsing and validating parameters, handling low-level TCP operations, address resolution, port parsing, etc.
- messages.c / messages.h → Functions for composing, validating, and parsing protocol messages

## Example
Start the server:
```bash
./approx-server -f coeffs.txt -p 12345
```
Start a client:
```bash
./approx-client -u Alice -s localhost -p 12345 -a
```
