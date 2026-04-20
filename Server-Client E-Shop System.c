#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#define NUM_PRODUCTS 20
#define NUM_CUSTOMERS 5
#define NUM_ORDERS 10
#define PORT 8080
#define SERVER_IP "127.0.0.1"

// Order types for the structured communication protocol
typedef enum {
    ORDER_BUY,
    ORDER_QUERY,
    ORDER_CANCEL
} OrderType;

// Request struct sent from client to server
typedef struct {
    int customer_id;
    int product_id;
    OrderType order_type;
    time_t timestamp;   // unix timestamp of when the request was created
} OrderRequest;

// Response struct sent from server to client
typedef struct {
    int success;        // 1 = success, 0 = failure
    double price;       // price of the product
    int item_count;     // remaining stock (used for QUERY)
    char message[64];   // human-readable status message
} OrderResponse;

// Product struct
typedef struct {
    char description[50];
    double price;
    int item_count;
    int request_count;
    int sold_count;
    int cancel_count;
    int failed_customers[NUM_CUSTOMERS];
} Product;

Product catalog[NUM_PRODUCTS];

// Initialize product catalog
void initialize_catalog() {
    srand(42); // fixed seed for reproducibility of catalog prices
    for (int i = 0; i < NUM_PRODUCTS; i++) {
        sprintf(catalog[i].description, "Product %d", i + 1);
        catalog[i].price = (rand() % 100 + 1);
        catalog[i].item_count = 2;
        catalog[i].request_count = 0;
        catalog[i].sold_count = 0;
        catalog[i].cancel_count = 0;
        for (int j = 0; j < NUM_CUSTOMERS; j++) {
            catalog[i].failed_customers[j] = 0;
        }
    }
}

// Process an incoming order request and fill in the response
void process_order(OrderRequest *req, OrderResponse *resp) {
    int pid = req->product_id;
    int cid = req->customer_id;

    // Format timestamp for logging
    char timebuf[32];
    struct tm *tm_info = localtime(&req->timestamp);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

    if (req->order_type == ORDER_QUERY) {
        resp->success = 1;
        resp->price = catalog[pid].price;
        resp->item_count = catalog[pid].item_count;
        snprintf(resp->message, sizeof(resp->message), "%.2f euros, %d in stock", catalog[pid].price, catalog[pid].item_count);
        printf("[%s] Client %d: QUERY product %d -> %s\n", timebuf, cid, pid + 1, resp->message);
        return;
    }

    if (req->order_type == ORDER_CANCEL) {
        // Cancel restores one unit if the product was previously sold to this customer
        if (catalog[pid].sold_count > 0) {
            catalog[pid].sold_count--;
            catalog[pid].item_count++;
            catalog[pid].cancel_count++;
            resp->success = 1;
            resp->price = catalog[pid].price;
            resp->item_count = catalog[pid].item_count;
            snprintf(resp->message, sizeof(resp->message), "Cancellation accepted, refund %.2f euros", catalog[pid].price);
        } else {
            resp->success = 0;
            resp->price = 0.0;
            resp->item_count = catalog[pid].item_count;
            snprintf(resp->message, sizeof(resp->message), "Nothing to cancel for product %d", pid + 1);
        }
        printf("[%s] Client %d: CANCEL product %d -> %s\n", timebuf, cid, pid + 1, resp->message);
        return;
    }

    // ORDER_BUY
    catalog[pid].request_count++;
    if (catalog[pid].item_count > 0) {
        catalog[pid].item_count--;
        catalog[pid].sold_count++;
        resp->success = 1;
        resp->price = catalog[pid].price;
        resp->item_count = catalog[pid].item_count;
        snprintf(resp->message, sizeof(resp->message), "Purchase complete, total %.2f euros", catalog[pid].price);
        printf("[%s] Client %d: BUY product %d -> %s\n", timebuf, cid, pid + 1, resp->message);
    } else {
        catalog[pid].failed_customers[cid] = 1;
        resp->success = 0;
        resp->price = 0.0;
        resp->item_count = 0;
        snprintf(resp->message, sizeof(resp->message), "Out of stock");
        printf("[%s] Client %d: BUY product %d -> %s\n", timebuf, cid, pid + 1, resp->message);
    }
}

// Print final sales report
void print_report() {
    int total_requests = 0;
    int successful_orders = 0;
    int failed_orders = 0;
    int total_cancels = 0;
    double total_turnover = 0.0;

    for (int i = 0; i < NUM_PRODUCTS; i++) {
        total_requests += catalog[i].request_count;
        successful_orders += catalog[i].sold_count;
        failed_orders += catalog[i].request_count - catalog[i].sold_count;
        total_cancels += catalog[i].cancel_count;
        total_turnover += catalog[i].sold_count * catalog[i].price;
    }

    printf("\n===== FINAL REPORT =====\n");
    printf("%d BUY requests: %d succeeded, %d failed\n",
           total_requests, successful_orders, failed_orders);
    printf("%d cancellations processed\n", total_cancels);
    printf("%d products sold, total turnover: %.2f euros\n",
           successful_orders, total_turnover);
}

// ── SERVER (parent process) ─────────────────────────────────────────────────

void run_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, NUM_CUSTOMERS) < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }

    printf("[Server] Listening on port %d...\n", PORT);

    // Accept one connection per customer and serve all their orders
    int client_fds[NUM_CUSTOMERS];
    for (int i = 0; i < NUM_CUSTOMERS; i++) {
        client_fds[i] = accept(server_fd, NULL, NULL);
        if (client_fds[i] < 0) { perror("accept"); exit(EXIT_FAILURE); }
        printf("[Server] Client %d connected\n", i);
    }

    // Serve orders round-robin until all clients are done
    int done[NUM_CUSTOMERS];
    memset(done, 0, sizeof(done));
    int finished = 0;

    while (finished < NUM_CUSTOMERS) {
        for (int i = 0; i < NUM_CUSTOMERS; i++) {
            if (done[i]) continue;

            OrderRequest req;
            ssize_t n = recv(client_fds[i], &req, sizeof(req), MSG_DONTWAIT);
            if (n == sizeof(req)) {
                OrderResponse resp;
                process_order(&req, &resp);
                send(client_fds[i], &resp, sizeof(resp), 0);
            } else if (n == 0) {
                // client closed connection
                done[i] = 1;
                finished++;
                close(client_fds[i]);
            }
        }
        usleep(10000); // 10ms polling interval
    }

    close(server_fd);
    print_report();
}

// ── CLIENT (child process) ──────────────────────────────────────────────────

void run_client(int customer_id) {
    // Small delay so the server is ready before clients connect
    usleep(100000 * (customer_id + 1));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect"); exit(EXIT_FAILURE);
    }

    srand(time(NULL) ^ (getpid() << 16));

    for (int j = 0; j < NUM_ORDERS; j++) {
        OrderRequest req;
        req.customer_id = customer_id;
        req.product_id  = rand() % NUM_PRODUCTS;
        req.timestamp   = time(NULL);

        // Distribution: ~70% BUY, ~20% QUERY, ~10% CANCEL
        int r = rand() % 10;
        if (r < 7)       req.order_type = ORDER_BUY;
        else if (r < 9)  req.order_type = ORDER_QUERY;
        else             req.order_type = ORDER_CANCEL;

        send(sock, &req, sizeof(req), 0);

        OrderResponse resp;
        recv(sock, &resp, sizeof(resp), 0);

        const char *type_str = (req.order_type == ORDER_BUY)    ? "BUY"    :
                               (req.order_type == ORDER_QUERY)  ? "QUERY"  : "CANCEL";
        printf("Client %d: %s response -> %s\n", customer_id, type_str, resp.message);
        sleep(1);
    }

    close(sock);
    exit(EXIT_SUCCESS);
}

// ── MAIN ────────────────────────────────────────────────────────────────────

int main() {
    initialize_catalog();

    // Fork one child process per customer
    for (int i = 0; i < NUM_CUSTOMERS; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) {
            run_client(i);  // child → client
        }
    }

    // Parent becomes the server
    run_server();

    // Wait for all children
    for (int i = 0; i < NUM_CUSTOMERS; i++) wait(NULL);

    return 0;
}
