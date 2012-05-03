print 1, 2, 3

x = 0

def staticmethod ():
  x = 1 + 2
  print x

class whoop:
    a = 0
    b = 0

    def __init__ (self):
        self.a = 1

    def update_a (self, x):
        self.a = x

x = whoop ()
x.update_a (1)

staticmethod ()
