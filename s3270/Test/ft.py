#!/usr/bin/env python3

import unittest
from subprocess import Popen, PIPE, DEVNULL
import requests

# s3270 file transfer tests
class TestS3270ft(unittest.TestCase):

    # s3270 DFT-mode file transfer test
    def test_s3270_ft_dft(self):

        # Start 'playback' to read s3270's output.
        playback = Popen(["playback", "-b", "-p", "9991",
            "s3270/Test/ft_dft.trc"], stdout=DEVNULL)

        # Start s3270.
        s3270 = Popen(["s3270", "127.0.0.1:9991"], stdin=PIPE, stdout=DEVNULL)

        # Feed s3270 some actions.
        s3270.stdin.write(b'transfer direction=send host=tso localfile=s3270/Test/fttext hostfile=fttext\n')
        s3270.stdin.write(b"PF(3)\n")
        s3270.stdin.flush()

        # Wait for the processes to exit.
        rc = playback.wait(timeout=2)
        self.assertEqual(rc, 0)
        s3270.stdin.close()
        s3270.wait(timeout=2)

    # s3270 CUT-mode file transfer test
    def test_s3270_ft_cut(self):

        # Start 'playback' to read s3270's output.
        playback = Popen(["playback", "-b", "-p", "9990",
            "s3270/Test/ft_cut.trc"], stdout=DEVNULL)

        # Start s3270.
        s3270 = Popen(["s3270", "-model", "2", "127.0.0.1:9990"], stdin=PIPE,
                stdout=DEVNULL)

        # Feed s3270 some actions.
        s3270.stdin.write(b'transfer direction=send host=vm "localfile=s3270/Test/fttext" "hostfile=ft text a"\n')
        s3270.stdin.write(b"String(logoff)\n")
        s3270.stdin.write(b"Enter()\n")
        s3270.stdin.flush()

        # Wait for the processes to exit.
        rc = playback.wait(timeout=2)
        self.assertEqual(rc, 0)
        s3270.stdin.close()
        s3270.wait(timeout=2)

if __name__ == '__main__':
    unittest.main()
