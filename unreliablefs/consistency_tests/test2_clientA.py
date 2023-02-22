#! /usr/bin/env python3

import os
import fs_util
import sys
import time
'''
This is ClientA.
'''

cs739_env_vars = [
    'CS739_CLIENT_A', 'CS739_CLIENT_A_PORT', 'CS739_CLIENT_B', 'CS739_CLIENT_B_PORT', 'CS739_SERVER', 'CS739_MOUNT_POINT'
]
ENV_VARS = {var_name: os.environ.get(var_name) for var_name in cs739_env_vars}
for env_var in ENV_VARS.items():
    print(env_var)
    assert env_var is not None
TEST_DATA_DIR = ENV_VARS['CS739_MOUNT_POINT'] + '/test_consistency'
FNAME = f'{TEST_DATA_DIR}/case2'
print(TEST_DATA_DIR)
TEST_CASE_NO = 2


def run_test():
    host_b = ENV_VARS['CS739_CLIENT_B']
    port_b = ENV_VARS['CS739_CLIENT_B_PORT']
    assert fs_util.test_ssh_access(host_b, port_b)
    signal_name_gen = fs_util.get_fs_signal_name()
    host_b = "-p " + port_b + " "+  host_b
    print(host_b)

    if not fs_util.path_exists(TEST_DATA_DIR):
        fs_util.mkdir(TEST_DATA_DIR)

    # init
    if not fs_util.path_exists(FNAME):
        fs_util.create_file(FNAME)

    init_str = fs_util.gen_str_by_repeat('0', 32768)
    fd = fs_util.open_file(FNAME)
    fs_util.write_file(fd, init_str)
    fs_util.close_file(fd)
    # open again
    fd = fs_util.open_file(FNAME)
    print("THE FILE IS ON AAAA ===== !!!!", FNAME)

    # time for client_b to work, host_b should read the all-zero file
    cur_signal_name = next(signal_name_gen)
    print("===============STARTING CLIENT B=====", cur_signal_name)
    fs_util.start_another_client(host_b, 1, 'B', cur_signal_name)
    # fs_util.start_another_client(host_b, 1, 'B', "python3 /tmp/test1_clientB.py")
    print("===============STARTed CLIENT B=====")

    # wait until client_b finish
    while True:
        removed = fs_util.poll_signal_remove(host_b, cur_signal_name)
        if removed:
            break
        time.sleep(1)
    print('Clientb finished')

    # client_b should have deleted the file
    assert not fs_util.path_exists(FNAME)
    fs_util.create_file(FNAME)

    # now let's write again
    cur_str = fs_util.gen_str_by_repeat('1', 32768)
    fd = fs_util.open_file(FNAME)
    fs_util.write_file(fd, cur_str)
    fs_util.close_file(fd)

    last_signal_name = cur_signal_name
    cur_signal_name = next(signal_name_gen)
    fs_util.send_signal(host_b, cur_signal_name)

    # done
    fs_util.record_test_result(TEST_CASE_NO, 'A', 'OK')


if __name__ == '__main__':
    run_test()
