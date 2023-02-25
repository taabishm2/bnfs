# importing os module 
import os
import time

target_file_path = "/users/chrahul5/bnfs/unreliablefs/build/unreliablefs/mnt/am_i_durable.txt"

# Creates a new file.
fd = os.open(target_file_path, os.O_RDWR)
os.close(fd)

time.sleep(1)

fd = os.open(target_file_path, os.O_RDWR)
lineb = os.read(fd, 1024)
line = lineb.decode('utf-8')
print(line)
print("I have read {} bytes".format(len(line)))
os.close(fd)

os.remove(target_file_path)