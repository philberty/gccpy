class date:
    def __init__ (self, x, y, z):
        self.x = x
        self.y = y
        self.z = z
    
    def print_date (self):
        print self.x, self.y, self.z
    
    def update (self):
        if self.x < 30:
            self.x = self.x + 1
        else:
            self.x = 1
            if self.y < 12:
                self.y = self.y + 1
            else:
                self.y = 1
                self.z = self.z + 1

x = date (1,2,3)
x.print_date ()
x.update ()
x.print_date ()
print 1
