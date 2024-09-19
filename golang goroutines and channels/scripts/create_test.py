import random

def generate_test(num_conn, num_instrument, num_order, output_file):
    # generate instruments
    alphabets = [chr(alphabet) for alphabet in range(ord("A"), ord("Z") + 1)]
    def generate_instruments():
        for alpha1 in alphabets:
            for alpha2 in alphabets:
                for alpha3 in alphabets:
                    yield alpha1 + alpha2 + alpha3
    generator = generate_instruments()
    instruments = [next(generator) for i in range(num_instrument)]

    # generate orders
    order_id = 1
    cancel_map = [list() for _ in range(num_conn)] # store existing order_id
    sides = ["B", "S", "C"]
    with open(output_file, 'w') as file:
        file.write(str(num_conn) + "\n")
        file.write("o\n")
        for i in range(num_order):
            conn = random.randint(0, num_conn-1)
            side = sides[random.randint(0, 2)]
            if side == "C" and len(cancel_map[conn]) == 0:
                continue
            if side == "C":
                cancel_id = random.choice(cancel_map[conn])
                order = str(conn) + " " + side + " " + str(cancel_id) + "\n"
            else:
                instrument = instruments[random.randint(0, num_instrument-1)]
                price = random.randint(100, 200)
                count = random.randint(100, 200)
                order = str(conn) + " " + side + " " + str(order_id) + " " + instrument + " " + str(price) + " " + str(count) + "\n"
                cancel_map[conn].append(order_id)
                order_id += 1

            file.write(order)

if __name__ == '__main__':
    num_conn = 1
    num_instrument = 100
    num_order = 500000
    output_file = "tests/test_single_conn_many_orders.in"
    generate_test(num_conn, num_instrument, num_order, output_file)
