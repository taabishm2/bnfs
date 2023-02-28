#! /usr/bin/env python3

import os
import fs_util
import sys
import time
'''
This is ClientB of who-is-the-winner [option-A] task.
'''

cs739_env_vars = [
    'CS739_CLIENT_A', 'CS739_CLIENT_A_PORT', 'CS739_CLIENT_B', 'CS739_CLIENT_B_PORT', 'CS739_SERVER', 'CS739_MOUNT_POINT'
]
ENV_VARS = {var_name: os.environ.get(var_name) for var_name in cs739_env_vars}
for env_var in ENV_VARS.items():
    print(env_var)
    assert env_var is not None
TEST_DATA_DIR = ENV_VARS['CS739_MOUNT_POINT'] + '/test_consistency'
FNAME = f'{TEST_DATA_DIR}/case6'
print(TEST_DATA_DIR)
TEST_CASE_NO = 6


def run_test():
    host_a = ENV_VARS['CS739_CLIENT_A']
    port_a = ENV_VARS['CS739_CLIENT_A_PORT']
    assert fs_util.test_ssh_access(host_a, port_a)
    signal_name_gen = fs_util.get_fs_signal_name()
    host_a = "-p " + port_a + " "+  host_a
    print(host_a)

    if not fs_util.path_exists(TEST_DATA_DIR):
        fs_util.mkdir(TEST_DATA_DIR)

    # init
    if not fs_util.path_exists(FNAME):
        fs_util.create_file(FNAME)

    init_str = fs_util.gen_str_by_repeat('B', 32768)
    fd = fs_util.open_file(FNAME)
    fs_util.write_file(fd, init_str)
    
    
    # Before closing file, wake up A.
    # TODO: call this in a thread.
    cur_signal_name = next(signal_name_gen)
    print("===============STARTING CLIENT A=====", cur_signal_name)
    fs_util.start_another_client(host_a, 6, 'A', cur_signal_name)
    print("===============STARTed CLIENT A=====")

    fs_util.close_file(fd)

    
    time.sleep(10)

    # read the old content. Since B didn't flush, file must contain old contents written by A.
    fd = fs_util.open_file(FNAME)
    cur_str = fs_util.read_file(fd, 1)
    if cur_str == 'B':
        print("B won")
    else:
        print("A won")

if __name__ == '__main__':
    run_test()

