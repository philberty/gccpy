calls = 0

def acker (m, n):
    global calls
    calls = calls + 1
    if m == 0:
        return n + 1
    elif n == 0:
        return acker (m - 1, 1)
    else:
        return acker (m - 1, acker (m, n - 1))

acker (3, 6)
print calls
