#! /usr/bin/env python3

import os
import fs_util
import sys
import time
'''
This is ClientA of who-is-the-winner [option-A] task.
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
    init_str = fs_util.gen_str_by_repeat('A', 32768)
    fd = fs_util.open_file(FNAME)
    fs_util.write_file(fd, init_str)

    print("===============================YAYAYAYAYYYYYY======")
    signal_name_gen = fs_util.get_fs_signal_name()

    cur_signal_name = next(signal_name_gen)
    fs_util.wait_for_signal(cur_signal_name)

    fs_util.close_file(fd)

if __name__ == '__main__':
    run_test()

