// This file contains declarations for the main Engine class. You will
// need to add declarations to this file as you develop your Engine.

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <chrono>
#include <unordered_map>
#include <queue>
#include <string>
#include <mutex>
#include <vector>
#include <optional>
#include <memory>
#include <thread>
#include "io.hpp"

enum Side {
	buy_side = 'B',
	sell_side = 'S',
};

struct Order {
	public:
		uint32_t order_id;
		uint32_t price;
		uint32_t count;
		Side side; 
		int64_t timestamp;
		uint32_t execution_id;
		std::thread::id thread_id;

		Order(uint32_t order_id, uint32_t price, uint32_t count, Side side): 
			order_id(order_id), price(price), count(count), side(side), execution_id(1), thread_id(std::this_thread::get_id()) {}
};

// The reason why we create this struct is because we want a mutex to protect each ListOfOrders for different instrument.
// So actions can be performend concurrently on multiple instruments by different threads at the same time without having to wait for one instrument to finish.
struct ListOfOrders {
	private:
		struct CompareSell {
			bool operator()(const std::shared_ptr<Order> order1, const std::shared_ptr<Order> order2) {
				if (order1->price != order2->price) {
					return order1->price > order2->price;
				}
				return order1->timestamp > order2->timestamp;
			}
		};

		struct CompareBuy {
			bool operator()(const std::shared_ptr<Order> order1, const std::shared_ptr<Order> order2) {
				if (order1->price != order2->price) {
					return order1->price < order2->price;
				}
				return order1->timestamp > order2->timestamp;
			}
		};

		std::mutex listMutex;
		std::priority_queue<std::shared_ptr<Order>, std::vector<std::shared_ptr<Order>>, CompareSell> sell_orders;
		std::priority_queue<std::shared_ptr<Order>, std::vector<std::shared_ptr<Order>>, CompareBuy> buy_orders;
		std::unordered_map<uint32_t, std::shared_ptr<Order>> cancel_map;

	public:

		void matchOrder(ClientCommand& activeOrder, Side side); 

		bool cancelOrder(uint32_t order_id, std::chrono::microseconds::rep outputTime);
};

struct OrderBooks {
	// Put in private variable, because we want to setup our own way to access, and dont allow public to access/edit their own way.
	private:
		std::mutex orderBookMutex;
		std::unordered_map<std::string, std::shared_ptr<ListOfOrders>> InstrumentToOrders;
		std::unordered_map<uint32_t, std::string> OrderIDToInstrument;
	
	public:
		OrderBooks () {};

		std::shared_ptr<ListOfOrders> getListOfOrders(std::string instrument, uint32_t orderID, bool addOrderToInstrument);
		std::string getInstrument(uint32_t orderID);

		// void printRemainingOrders();
};



struct Engine
{
public:
	void accept(ClientConnection conn);
	Engine() {order_books = std::make_shared<OrderBooks>();}

private:
	void connection_thread(ClientConnection conn);
	std::shared_ptr<OrderBooks> order_books;
};

inline std::chrono::microseconds::rep getCurrentTimestamp() noexcept
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#endif
