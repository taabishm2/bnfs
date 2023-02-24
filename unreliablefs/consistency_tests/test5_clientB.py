#! /usr/bin/env python3

import os
import fs_util
import sys
import test2_clientA as test5  # Simply do not want to do test2_common
'''
This is ClientB.
'''


def run_test():
    printf("===============================YAYAYAYAYYYYYY======")
    signal_name_gen = fs_util.get_fs_signal_name()

    cur_signal_name = next(signal_name_gen)
    fs_util.record_test_result(test5.TEST_CASE_NO, 'B',
                               f'START fname:{test5.FNAME}')
    fs_util.wait_for_signal(cur_signal_name)

    # first execution, read all-zero file
    if not fs_util.path_exists(test5.FNAME):
        fs_util.record_test_result(test5.TEST_CASE_NO, 'B', 'not exist')
        sys.exit(1)
    fd = fs_util.open_file(test5.FNAME)
    read_len = 32768
    read_str = fs_util.read_file(fd, read_len, 0)
    if len(read_str) != read_len:
        fs_util.record_test_result(test5.TEST_CASE_NO, 'B',
                                   f'read_len:{len(read_str)}')
        sys.exit(1)
    for rc in read_str:
        if rc != '0':
            fs_util.record_test_result(test5.TEST_CASE_NO, 'B',
                                       f'read_str:{read_str}')
            sys.exit(1)

    #  Delete file
    cur_str = fs_util.gen_str_by_repeat('1', 32768)
    fd = fs_util.open_file(test4.FNAME)
    fs_util.write_file(fd, cur_str)
    # dont call close
    fs_util.record_test_result(test4.TEST_CASE_NO, 'B',
                               f'Finished Write without close of file')

    # crash client B
    for line in os.popen("ps ax | grep unreliablefs | grep -v grep"):
        fields = line.split()
            
        # extracting Process ID from the output
        pid = fields[0]
            
        # terminating process
        os.kill(int(pid), signal.SIGKILL)
    print("./unreliablefs client process successfully terminated on client B")

    # start client b on sweksha's machine.
    os.system("/users/chrahul5/sweksha/bnfs/unreliablefs/build/unreliablefs/unreliablefs /users/chrahul5/sweksha/bnfs/unreliablefs/build/unreliablefs/tmp -d -basedir=/")

    # call close(). Since the client rebooted, the temp files should have been
    # cleared off. So the close() will flush the cached files containing old data
    fs_util.close_file(fd)

    # read file with all 0's
    if not fs_util.path_exists(test5.FNAME):
        fs_util.record_test_result(test5.TEST_CASE_NO, 'B', 'not exist')
        sys.exit(1)
    fd = fs_util.open_file(test5.FNAME)
    read_len = 32768
    read_str = fs_util.read_file(fd, read_len, 0)
    if len(read_str) != read_len:
        fs_util.record_test_result(test5.TEST_CASE_NO, 'B',
                                   f'read_len:{len(read_str)}')
        sys.exit(1)
    for rc in read_str:
        if rc != '0':
            fs_util.record_test_result(test5.TEST_CASE_NO, 'B',
                                       f'read_str:{read_str}')
            sys.exit(1)

    # done
    fs_util.record_test_result(test5.TEST_CASE_NO, 'B', 'OK')


if __name__ == '__main__':
    run_test()
