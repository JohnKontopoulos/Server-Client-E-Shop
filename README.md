# Server–Client E-Shop System

A multi-process client–server system written in C on Linux, simulating an e-commerce workflow using TCP sockets and anonymous pipes.

## Overview

The project implements an e-shop architecture where a **parent process** acts as the server and **child processes** (forked via `fork()`) act as customers. Communication between server and clients is handled through **TCP sockets** using a structured binary protocol.

## Features

- Multi-process architecture using `fork()`
- TCP socket communication (server listens on port 8080, clients connect)
- Structured communication protocol with typed request/response structs
- Three order types: **BUY**, **QUERY**, and **CANCEL**
- Timestamped requests for logging
- Final sales report with total turnover, successful orders, and cancellations

## Communication Protocol

### Request (Client → Server)

| Field         | Type       | Description                        |
|---------------|------------|------------------------------------|
| `customer_id` | `int`      | Unique identifier of the customer  |
| `product_id`  | `int`      | Identifier of the requested product|
| `order_type`  | `enum`     | `BUY`, `QUERY`, or `CANCEL`        |
| `timestamp`   | `time_t`   | Unix timestamp of the request      |

### Response (Server → Client)

| Field        | Type        | Description                        |
|--------------|-------------|------------------------------------|
| `success`    | `int`       | `1` = success, `0` = failure       |
| `price`      | `double`    | Price of the product               |
| `item_count` | `int`       | Remaining stock                    |
| `message`    | `char[64]`  | Human-readable status message      |

## How It Works

1. The catalog is initialized with 20 products, each with 2 units in stock and a random price.
2. The parent process starts a TCP server and listens for incoming connections.
3. Five child processes are forked, each acting as a customer that connects to the server and sends 10 orders.
4. Orders are distributed as ~70% BUY, ~20% QUERY, ~10% CANCEL.
5. The server processes each order and sends back a response.
6. After all clients disconnect, the server prints a final report.

## Build & Run

> Requires a Linux environment (or WSL on Windows).

```bash
gcc -o eshop "Server-Client E-Shop System.c"
./eshop
```

## Sample Output

```
[Server] Listening on port 8080...
[Server] Client 0 connected
[20:53:28] Client 0: BUY product 13 -> Purchase complete, total 18.00 euros
[20:53:28] Client 1: QUERY product 6 -> 59.00 euros, 2 in stock
[20:53:29] Client 2: CANCEL product 4 -> Cancellation accepted, refund 42.00 euros
...

===== FINAL REPORT =====
31 BUY requests: 26 succeeded, 5 failed
1 cancellations processed
26 products sold, total turnover: 1367.00 euros
```

## Project Structure

```
.
└── Server-Client E-Shop System.c   # Full source code (server + client + protocol)
```

## Course Info

Second assignment for the **Programming 3** course.  
Department of Computer Science & Engineering.
