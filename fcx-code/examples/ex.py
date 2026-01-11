a, b = 0, 1
for i in range(300):
    a, b = b, a + b
print(f'fib(300) = {a}')
print(f'fits in i128? {a < 2**127}')