#include <iostream>
#include <thread>

#include "io.hpp"
#include "engine.hpp"
#include <algorithm>

void Engine::accept(ClientConnection connection)
{
	auto thread = std::thread(&Engine::connection_thread, this, std::move(connection));
	thread.detach();
}

void Engine::connection_thread(ClientConnection connection)
{
	while(true)
	{
		ClientCommand input {};
		switch(connection.readInput(input))
		{
			case ReadResult::Error: SyncCerr {} << "Error reading input" << std::endl;
			case ReadResult::EndOfFile: return;
			case ReadResult::Success: break;
		}
		
		// Functions for printing output actions in the prescribed format are
		// provided in the Output class:
		// auto output_time = getCurrentTimestamp();

		switch(input.type)
		{
			case input_cancel: {
				// SyncCerr {} << "Got cancel: ID: " << input.order_id << std::endl;

				// Check whether if this order ID exists in our orderbooks, if it doesn't exist, it is already cancelled or it simply does not exist. So we reject.
				std::string instrument = order_books->getInstrument(input.order_id);

				if (!instrument.empty()) {
					// Order is possibly still in orderbooks
					// Retrieve list of orders for this instrument
					// Then search in the listoforders.
					std::shared_ptr<ListOfOrders> listOrders = order_books->getListOfOrders(instrument, input.order_id, false);
					listOrders->cancelOrder(input.order_id, getCurrentTimestamp());
				}
				else {
					// Never see order before, Reject Cancel Order
					Output::OrderDeleted(input.order_id, false, getCurrentTimestamp());
				}
				
				break;
			}

			// Cases for input_buy and input_sell
			case input_buy: {
				// SyncCerr {}
				//     << "Got order: " << static_cast<char>(input.type) << " " << input.instrument << " x " << input.count << " @ "
				//     << input.price << " ID: " << input.order_id << std::endl;

				// Check orderlist for this instrument first

				// SyncCerr {}
				//     << "Attempting to get listOrders for buy order, order id: " << input.order_id  << std::endl;

				std::shared_ptr<ListOfOrders> listOrders = order_books->getListOfOrders(input.instrument, input.order_id, true);
				// SyncCerr {}
				//     << "Managed to get listOrders for buy order, order id: " << input.order_id << std::endl;

				// Match active order against resting orders
				listOrders->matchOrder(input, Side::buy_side);
				break;
			}

			case input_sell: {
				// SyncCerr {}
				//     << "Got order: " << static_cast<char>(input.type) << " " << input.instrument << " x " << input.count << " @ "
				//     << input.price << " ID: " << input.order_id << std::endl;

				// Check buy side orderlist for this instrument first
				// SyncCerr {}
				//     << "Attempting to get listOrders for sell order, order id: " << input.order_id  << std::endl;
				std::shared_ptr<ListOfOrders> listOrders = order_books->getListOfOrders(input.instrument, input.order_id, true);

				// SyncCerr {}
				//     << "Managed to get listOrders for sell order, order id: " << input.order_id  << std::endl;

				listOrders->matchOrder(input, Side::sell_side);
				break;
			}

			default: {
				SyncCerr {}
				    << "Got order: " << static_cast<char>(input.type) << " " << input.instrument << " x " << input.count << " @ "
				    << input.price << " ID: " << input.order_id << std::endl;

				SyncCerr {}
				    << "Reached default case - this should not happen" << std::endl;
				
				// order_books->printRemainingOrders();

				// Remember to take timestamp at the appropriate time, or compute
				// an appropriate timestamp!

				break;
			}
		}

		

		// Additionally:

		// Remember to take timestamp at the appropriate time, or compute
		// an appropriate timestamp!

		// Check the parameter names in `io.hpp`.
		// Output::OrderExecuted(123, 124, 1, 2000, 10, output_time);
	}
}


bool ListOfOrders::cancelOrder(uint32_t order_id, std::chrono::microseconds::rep outputTime)
{
	// SyncCerr {} << "Attempting to get matchOrder Mutex, order id: " << order_id  << std::endl;
	std::lock_guard<std::mutex> lock(listMutex);
	// SyncCerr {} << "Managed to get matchOrder Mutex, order id: " << order_id  << std::endl;

	if (cancel_map.contains(order_id) 
	&& cancel_map[order_id]->count != 0 
	&& cancel_map[order_id]->timestamp <= outputTime 
	&& std::this_thread::get_id() == cancel_map[order_id]->thread_id) {
		// For an order to be valid for cancel it must have all of:
		// 1. Be added into book
		// 2. Have non zero count -> Not executed/cancelled yet
		// 3. Valid timestamp
		// 4. Be added by this thread
		cancel_map[order_id]->count = 0; // Set count to 0 to be popped later
		Output::OrderDeleted(order_id, true, getCurrentTimestamp());
		return true;
	}
	Output::OrderDeleted(order_id, false, getCurrentTimestamp());
	return false;
}

void ListOfOrders::matchOrder(ClientCommand& activeOrder, Side side)
{
	if (side == Side::buy_side) {
		std::shared_ptr<Order> buy_order = std::make_shared<Order>(Order(activeOrder.order_id, activeOrder.price, activeOrder.count, Side::buy_side));
		std::lock_guard<std::mutex> lock(listMutex);
		// Current active order is trying to buy, hence we need to search into the sell side.
		while (!sell_orders.empty()) {
			auto restingOrder = sell_orders.top();
			if (restingOrder->count == 0) {
				// Pop cancelled order
				sell_orders.pop();
				continue;
			}
			if (buy_order->price >= restingOrder->price && getCurrentTimestamp() >= restingOrder->timestamp) {
				uint32_t tradedAmount = std::min(restingOrder->count, buy_order->count);
				if (tradedAmount == 0) {
					return;
				}
				buy_order->count -= tradedAmount;
				restingOrder->count -= tradedAmount;
				if (restingOrder->count == 0) {
					Output::OrderExecuted(restingOrder->order_id, buy_order->order_id, restingOrder->execution_id, restingOrder->price, tradedAmount, getCurrentTimestamp());
					sell_orders.pop();
				} else {
					Output::OrderExecuted(restingOrder->order_id, buy_order->order_id, restingOrder->execution_id, restingOrder->price, tradedAmount, getCurrentTimestamp());
					restingOrder->execution_id++;
				}
			} else {
				break;
			}
		}
		
		if (buy_order->count > 0) {
			// Add to buy order list
			auto outputTimestamp = getCurrentTimestamp();
			buy_order->timestamp = outputTimestamp;
			buy_orders.push(buy_order);
			cancel_map[buy_order->order_id] = buy_order;
			Output::OrderAdded(buy_order->order_id, activeOrder.instrument, buy_order->price, buy_order->count, activeOrder.type == CommandType::input_sell, outputTimestamp);
		}
	}
	else {
		// Current active order is trying to sell, hence we need to search into the buy side.
		std::shared_ptr<Order> sell_order = std::make_shared<Order>(Order(activeOrder.order_id, activeOrder.price, activeOrder.count, Side::sell_side));
		std::lock_guard<std::mutex> lock(listMutex);
		while (!buy_orders.empty()) {
			auto restingOrder = buy_orders.top();
			if (restingOrder->count == 0) {
				// Pop cancelled order
				buy_orders.pop();
				continue;
			}
			if (sell_order->price <= restingOrder->price && getCurrentTimestamp() >= restingOrder->timestamp) {
				uint32_t tradedAmount = std::min(restingOrder->count, sell_order->count);
				if (tradedAmount == 0) {
					return;
				}
				sell_order->count -= tradedAmount;
				restingOrder->count -= tradedAmount;
				if (restingOrder->count == 0) {
					Output::OrderExecuted(restingOrder->order_id, sell_order->order_id, restingOrder->execution_id, restingOrder->price, tradedAmount, getCurrentTimestamp());
					buy_orders.pop();
				} else {
					Output::OrderExecuted(restingOrder->order_id, sell_order->order_id, restingOrder->execution_id, restingOrder->price, tradedAmount, getCurrentTimestamp());
					restingOrder->execution_id++;
				}
			} else {
				break;
			}
		}
		
		if (sell_order->count > 0) {
			// Add to sell order list
			auto outputTimestamp = getCurrentTimestamp();
			sell_order->timestamp = outputTimestamp;
			sell_orders.push(sell_order);
			cancel_map[sell_order->order_id] = sell_order;
			Output::OrderAdded(sell_order->order_id, activeOrder.instrument, sell_order->price, sell_order->count, activeOrder.type == CommandType::input_sell, outputTimestamp);
		}
	}
}


std::shared_ptr<ListOfOrders> OrderBooks::getListOfOrders(std::string instrument, uint32_t orderID, bool addOrderToInstrument)
{
	std::lock_guard<std::mutex> lock(orderBookMutex);
	
	if (addOrderToInstrument) {
		OrderIDToInstrument.emplace(orderID, instrument);
	}

	auto it = InstrumentToOrders.find(instrument);
	if (it == InstrumentToOrders.end()) {
		// Item not found, create a ListOfOrders for this instrument
		std::shared_ptr<ListOfOrders> newList = std::make_shared<ListOfOrders>();

		InstrumentToOrders.insert(std::pair<std::string, std::shared_ptr<ListOfOrders>>(instrument, newList));
		return newList;
	}
	else {
		return it->second;
	}
}

std::string OrderBooks::getInstrument(uint32_t orderID)
{
	std::lock_guard<std::mutex> lock(orderBookMutex);

	auto it = OrderIDToInstrument.find(orderID);

	if (it == OrderIDToInstrument.end()) {
		// Not found
		return "";
	}
	else {
		// Return the instrument name
		return it->second;
	}

}



// void OrderBooks::printRemainingOrders()
// {
// 	std::lock_guard<std::mutex> lock(orderBookMutex);
// 	std::cout << "---Remaining Orders---" << std::endl;
// 	for (const auto& item : InstrumentToOrders) {

// 		std::cout << "Instrument: "<< item.first << std::endl;

// 		for (const auto& order: item.second)
//         uint32_t orderID = item.first;
//         const std::string& instrument = item.second.first;
//         Side side = item.second.second;

//         // Print the order ID, instrument name, and side
//         std::cout << "Order ID: " << orderID << ", Instrument: " << instrument
//                   << ", Side: " << (side == Side::buy_side ? "Buy" : "Sell") << std::endl;
//     }
// 	std::cout << "--------END-----------" << std::endl;
// }	
