def fib (n):
    a = 0
    b = 1
    while b < n:
        print b
        tmp = a
        a = b
        b = a + tmp
fib (5)
