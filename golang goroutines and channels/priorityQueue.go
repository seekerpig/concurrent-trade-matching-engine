package main

type Order struct {
	in          input
	timestamp   int64
	executionId uint32
	index       int
	done        chan struct{}
	finished    bool
}

type PriorityQueue []*Order

func (pq PriorityQueue) Len() int { return len(pq) }

func (pq PriorityQueue) Less(i, j int) bool {
	// We want Pop to give us the highest, not lowest, priority so we use greater than here.
	if pq[i].in.price == pq[j].in.price {
		return pq[i].timestamp < pq[j].timestamp
	}
	if pq[i].in.orderType == inputBuy {
		// priorityqueue of buy orders, higher price comes first
		return pq[i].in.price > pq[j].in.price
	} else {
		// priorityqueue of sell orders, lower price comes first
		return pq[i].in.price < pq[j].in.price
	}
}

func (pq PriorityQueue) Swap(i, j int) {
	pq[i], pq[j] = pq[j], pq[i]
	pq[i].index = i
	pq[j].index = j
}

func (pq *PriorityQueue) Push(x any) {
	n := len(*pq)
	item := x.(*Order)
	item.index = n
	*pq = append(*pq, item)
}

func (pq *PriorityQueue) Pop() any {
	old := *pq
	n := len(old)
	item := old[n-1]
	old[n-1] = nil  // avoid memory leak
	item.index = -1 // for safety
	*pq = old[0 : n-1]
	return item
}
