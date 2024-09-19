package main

import "C"
import (
	"container/heap"
	"context"
	"fmt"
	"io"
	"net"
	"os"
	"time"
)

type Engine struct{}

var BUFFER_SIZE = 10000

func (e *Engine) accept(ctx context.Context, conn net.Conn, inputChan chan<- Order) {
	go func() {
		<-ctx.Done()
		conn.Close()
	}()
	go handleConn(conn, inputChan)
}

func handleConn(conn net.Conn, inputChan chan<- Order) {
	defer conn.Close()
	for {
		in, err := readInput(conn)
		if err != nil {
			if err != io.EOF {
				_, _ = fmt.Fprintf(os.Stderr, "Error reading input: %v\n", err)
			}
			return
		}
		done := make(chan struct{})
		order := Order{
			in:          in,
			timestamp:   0,
			executionId: 1,
			index:       0,
			done:        done,
		}
		inputChan <- order
		<-done // wait for order to be done before starting on the next one
	}
}

func handleInput(inputChan <-chan Order) {
	// cancelMap maps orderId to its instrument
	cancelMap := make(map[uint32]string)
	instrumentMap := make(map[string]chan Order)

	for {
		select {
		case order := <-inputChan:
			switch order.in.orderType {
			case inputCancel:
				if instrument, ok := cancelMap[order.in.orderId]; ok {
					// order found, send cancel order to its instrument
					instrumentMap[instrument] <- order
				} else {
					// order not found
					outputOrderDeleted(order.in, false, GetCurrentTimestamp())
				}
				break
			default:
				// update cancelMap
				cancelMap[order.in.orderId] = order.in.instrument
				if _, ok := instrumentMap[order.in.instrument]; !ok {
					// first time encountering this instrument, create channel and start new goroutine
					instrumentChan := make(chan Order, BUFFER_SIZE)
					instrumentMap[order.in.instrument] = instrumentChan
					go handleInstrument(instrumentChan)
				}
				// send input to handleInstrument
				instrumentMap[order.in.instrument] <- order
			}
		}
	}
}

func handleInstrument(instrumentChan <-chan Order) {
	cancelMap := make(map[uint32]input)       // cancelMap maps orderId to its input
	buyChan := make(chan Order, BUFFER_SIZE)  // channel for sending orders to handleSide(buy)
	sellChan := make(chan Order, BUFFER_SIZE) // channel for sending orders to handleSide(sell)
	buyDone := make(chan struct{}, 1)         // channel that acts as a mutex for buy orders
	buyDone <- struct{}{}
	sellDone := make(chan struct{}, 1) // channel that acts as a mutex for sell orders
	sellDone <- struct{}{}
	go handleSide(inputBuy, inputSell, buyChan, sellChan, buyDone)  // handleSide(buy)
	go handleSide(inputSell, inputBuy, sellChan, buyChan, sellDone) // handleSide(sell)

	var currBuy *Order
	var currSell *Order
	for {
		select {
		case order := <-instrumentChan:
			switch order.in.orderType {
			case inputCancel:
				// fmt.Fprintf(os.Stderr, "Instrument received cancel: %v\n", order.in.orderId)
				if inSaved, ok := cancelMap[order.in.orderId]; ok {
					// order found, send cancel order to its instrument
					if inSaved.orderType == inputBuy {
						// trying to cancel a buy order, find it on handleSide(sell), since it stores buy orders
						sellChan <- order
					} else {
						// trying to cancel a sell order, find it on handleSide(buy), since it stores sell orders
						buyChan <- order
					}
				} else {
					// order not found
					outputOrderDeleted(order.in, false, GetCurrentTimestamp())
				}
			case inputBuy:
				// fmt.Fprintf(os.Stderr, "Instrument received: %v\n", order.in.orderId)
				<-buyDone // wait for buy to be ready
				cancelMap[order.in.orderId] = order.in

				if currSell != nil && !currSell.finished && currSell.in.price <= order.in.price {
					// in this case, current sell order is a match with current buy order
					// we cannot process this buy order until the sell order is done processing
					// if we dont wait, then it might not be able to match with the sell order as it hasnt been added to the pq
					<-sellDone // wait for sell order to finish
					buyChan <- order
					// set currBuy before unlocking sellDone in the following line, so when a sell order is processing, it will consider currBuy first
					currBuy = &order
					sellDone <- struct{}{}
				} else {
					// if there isnt a current buy order
					// sell order can be processed safely for sure
					// if there is a current buy order, but it doesnt match with our sell order
					// sell order can still be processed safely, since current buy order and current sell order can process independently without affecting each other
					// in this case, phase level concurrency is achieved as orders of the same instrument and of different sides are processed concurrently
					buyChan <- order
					currBuy = &order
				}
			case inputSell:
				// fmt.Fprintf(os.Stderr, "Instrument received: %v\n", order.in.orderId)
				// same implementation as buy orders above, just flipped the sides
				<-sellDone
				cancelMap[order.in.orderId] = order.in

				if currBuy != nil && !currBuy.finished && currBuy.in.price >= order.in.price {
					<-buyDone
					sellChan <- order
					currSell = &order
					buyDone <- struct{}{}
				} else {
					sellChan <- order
					currSell = &order
				}
			}
		}
	}
}

func handleSide(currSide inputType, oppSide inputType, currSideChan <-chan Order, otherSideChan chan<- Order, done chan struct{}) {
	pq := make(PriorityQueue, 0)         // priorityQueue of orders
	cancelMap := make(map[uint32]*Order) // map orderId to Order

	// function for comparing prices
	comparePrice := func(price1 uint32, price2 uint32) bool {
		if currSide == inputBuy {
			return price1 <= price2
		} else {
			return price1 >= price2
		}
	}

	for {
		select {
		case order := <-currSideChan:
			switch order.in.orderType {
			case currSide:
				// received order for current side, to be executed
				// fmt.Fprintf(os.Stderr, "Side received: %v\n", order.in.orderId)
				for order.in.count > 0 {
					if pq.Len() > 0 && pq[0].in.count == 0 {
						heap.Pop(&pq)
						continue
					}
					if pq.Len() > 0 && comparePrice(pq[0].in.price, order.in.price) {
						// can execute
						countExecuted := min(pq[0].in.count, order.in.count)
						order.in.count -= countExecuted
						pq[0].in.count -= countExecuted
						outputOrderExecuted(pq[0].in.orderId, order.in.orderId, pq[0].executionId, pq[0].in.price, countExecuted, GetCurrentTimestamp())
						pq[0].executionId += 1
					} else {
						// cannot execute
						break
					}
				}
				if order.in.count > 0 {
					// leftover count, send it to opposite side to add to its pq
					// this line might be able to cause a deadlock
					// in the case where otherSideChan's buffer is full, and both buy and sell side are trying to send to otherSideChan
					// then otherSideChan will block on for both buy and sell side, and neither are free to receive
					// one solution is to use a large buffer size
					otherSideChan <- order
				} else {
					// nothing left to do for this order
					order.finished = true
					order.done <- struct{}{} // signal that current order is done, next order can proceed
				}
				done <- struct{}{}
			case oppSide:
				// received order for other side, to be pushed to priority queue
				// fmt.Fprintf(os.Stderr, "Other side received: %v\n", order.in.orderId)
				order.timestamp = GetCurrentTimestamp()
				heap.Push(&pq, &order)
				outputOrderAdded(order.in, order.timestamp)
				cancelMap[order.in.orderId] = &order
				order.finished = true
				order.done <- struct{}{} // signal that current order is done, next order can proceed
			case inputCancel:
				// received cancel order
				// fmt.Fprintf(os.Stderr, "Cancel side received: %v\n", order.in.orderId)
				if orderSaved, ok := cancelMap[order.in.orderId]; ok && orderSaved.in.count > 0 {
					outputOrderDeleted(order.in, true, GetCurrentTimestamp())
					orderSaved.in.count = 0 // set its count to 0
					delete(cancelMap, order.in.orderId)
				} else {
					// order is missing in cancelMap or already cancelled
					outputOrderDeleted(order.in, false, GetCurrentTimestamp())
				}
				order.finished = true
				order.done <- struct{}{} // signal that current order is done, next order can proceed
			}
		}
	}
}

func GetCurrentTimestamp() int64 {
	return time.Now().UnixNano()
}
