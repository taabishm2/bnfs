# importing os module 
import os
  
target_file_path = "/users/chrahul5/bnfs/unreliablefs/build/unreliablefs/mnt/am_i_durable.txt"

# Creates a new file.
fd = os.open(target_file_path, os.O_CREAT | os.O_RDWR)

line1 = str.encode("this is line 1\n")
line2 = str.encode("this is line 2\n")

# W1
numBytes1 = os.write(fd, line1)
print("I have writted {} bytes".format(numBytes1))

# W1
numBytes2 = os.write(fd, line2)
print("I have writted {} bytes".format(numBytes2))

os.close(fd)