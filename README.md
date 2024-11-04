## Goal of this project is to practice parallel and concurrent programming with different concurrency paradigms, following NUS CS3211 module.
1. C++ (Threads and Locks) to implement a Matching Engine
2. Golang (Goroutines and Channels) to implement a Matching Engine
3. Rust (Async Programming with Tokio) to implement async programming when dealing with CPU and I/O intensive tasks.

## Definition of a Matching engine
A matching engine is a component that allows the matching of buy and sell orders inside an exchange. When an exchange receives a new order, it is considered ‘active’, and it will first try to match the active order against existing orders, called ‘resting’ orders. In case the exchange cannot match the active order against any resting orders, it will store the active order in an order book, so that it can potentially match it later. When an order is added to the order book, it is no longer considered ‘active’ and is now considered ‘resting’.
Your exchange will match orders using the price-time priority rule. Order matching only happens between active orders (new orders not added to the order book yet), and resting orders (orders that have already been added to the order book). 

This rule for matching two orders on an exchange is expressed using the following conditions – which must all be true for the matching to happen:
- The side of the two orders must be different (i.e. a buy order must match against a sell orders or vice versa).
- The instrument of the two orders must be the same (i.e. an order for “GOOG” must match against another order for “GOOG”).
- The size of the two orders must be greater than zero.
- The price of the buy order must be greater or equal to the price of the sell order.
- In case multiple orders can be matched, the order with the best price is matched first. For sell orders, the best price is the lowest; for buy orders, the best price is the highest.

If there are still multiple matchable orders, the order that was added to the order book the earliest (ie. the order whose “added to order book” log has the earliest timestamp) will be matched first. This is to ensure fairness towards the orders that have waited the longest.
If an active order cannot match with any resting order, it may be added to the order book, at which point it becomes a resting order.
Orders can be partially matched in case the size of the other order is lower. Consequently, orders can be matched multiple times.
