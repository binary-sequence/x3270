#!/usr/bin/env python3
#
# Copyright (c) 2021-2022 Paul Mattes.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the names of Paul Mattes nor the names of his contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# b3270 JSON tests

import unittest
from subprocess import Popen, PIPE, DEVNULL
import json
import os
import sys
import time
import TestCommon

class TestB3270Json(unittest.TestCase):

    # Set up procedure.
    def setUp(self):
        self.children = []

    # Tear-down procedure.
    def tearDown(self):
        # Tidy up the children.
        for child in self.children:
            child.kill()
            child.wait()

    # b3270 NVT JSON smoke test
    def test_b3270_nvt_json_smoke(self):

        # Start 'nc' to read b3270's output.
        nc = TestCommon.copyserver()

        # Start b3270.
        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=DEVNULL)
        self.children.append(b3270)

        # Feed b3270 some actions.
        j = { "run": { "actions": [ { "action": "Open", "args": [f"a:c:t:127.0.0.1:{nc.port}"]}, { "action": "String", "args": ["abc"] }, { "action": "Enter" }, { "action": "Disconnect" } ] } }
        b3270.stdin.write(json.dumps(j).encode('utf8') + b'\n')
        b3270.stdin.flush()

        # Make sure they are passed through.
        out = nc.data()
        self.assertEqual(b"abc\r\n", out)

        # Wait for the processes to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

    # b3270 JSON single test
    def test_b3270_json_single(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)
        self.children.append(b3270)

        # Feed b3270 an action.
        b3270.stdin.write(b'"set startTls"\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('run-result' in out)
        result = out['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('true', result['text'][0])

    # b3270 JSON multiple test
    def test_b3270_json_multiple(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)
        self.children.append(b3270)

        # Feed b3270 two sets of actions, which it will run concurrently and complete
        # in reverse order.
        b3270.stdin.write(b'"Wait(0.1,seconds) Set(startTls) Quit()" "Set(insertMode)"\n')
        b3270.stdin.flush()

        # Get the output before waiting for b3270 to exit. Otherwise it
        # hangs trying to write to its stdout (on Windows).
        # Individual timed reads are used here because communicate() closes stdin and that will
        # cause b3270 to exit prematurely.
        errmsg = 'b3270 did not produce expected output'
        _ = TestCommon.timed_readline(b3270.stdout, 2, errmsg)
        insert_mode = json.loads(TestCommon.timed_readline(b3270.stdout, 2, errmsg).decode('utf8'))
        start_tls = json.loads(TestCommon.timed_readline(b3270.stdout, 2, errmsg).decode('utf8'))
        b3270.stdin.close()
        b3270.stdout.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('run-result' in insert_mode)
        result = insert_mode['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('false', result['text'][0])

        self.assertTrue('run-result' in start_tls)
        result = start_tls['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('true', result['text'][0])

    # b3270 JSON split-line test
    def test_b3270_json_split(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)
        self.children.append(b3270)

        # Feed b3270 an action.
        b3270.stdin.write(b'{\n"run"\n:{"actions"\n:{"action":"set"\n,"args":["startTls"]\n}}}\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('run-result' in out)
        result = out['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('true', result['text'][0])

    # b3270 JSON semantic error test
    def test_b3270_json_semantic_error(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)
        self.children.append(b3270)

        # Feed b3270 an action.
        b3270.stdin.write(b'{\n"run"\n:{"actiobs"\n:{"action":"set"\n,"args":["startTls"]\n}}}\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('ui-error' in out)
        ui_error = out['ui-error']
        self.assertTrue('fatal' in ui_error)
        self.assertFalse(ui_error['fatal'])

    # b3270 JSON syntax error test
    def test_b3270_json_syntax_error(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE,
                stderr=DEVNULL)
        self.children.append(b3270)

        # Feed b3270 an action.
        b3270.stdin.write(b'{\n"run"\n:{"actiobs"\n:{"action":"set"\n,"args":["startTls"]\n}}?\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('ui-error' in out)
        ui_error = out['ui-error']
        self.assertTrue('fatal' in ui_error)
        self.assertTrue(ui_error['fatal'])

    # b3270 JSON not-indented test
    def test_b3270_json_default(self):

        # Start b3270.
        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE, stderr=DEVNULL)
        self.children.append(b3270)

        # Grab its output.
        out = b3270.communicate(timeout=2)[0].decode('utf8').split(os.linesep)
        self.assertEqual(2, len(out))
        self.assertTrue(out[0].startswith('{"initialize":['))
        self.assertTrue(out[0].endswith(']}'))
        self.assertEqual('', out[1])

        rc = b3270.wait(timeout=2)
        self.assertEqual(0, rc)

    # b3270 JSON indented test
    def test_b3270_json_indented(self):

        # Start b3270.
        b3270 = Popen(['b3270', '-json', '-indent'], stdin=PIPE, stdout=PIPE, stderr=DEVNULL)
        self.children.append(b3270)

        # Grab its output.
        out = b3270.communicate(timeout=2)[0].decode('utf8').split(os.linesep)
        self.assertEqual('{', out[0])
        self.assertEqual('  "initialize": [', out[1])
        self.assertEqual('    {', out[2])
        self.assertEqual('      "hello": {', out[3])
        self.assertEqual('  ]', out[-3])
        self.assertEqual('}', out[-2])
        self.assertEqual('', out[-1])

        rc = b3270.wait(timeout=2)
        self.assertEqual(0, rc)

    # b3270 JSON socket test
    def test_b3270_json_socket(self):

        # Listen for a connection from b3270.
        l = TestCommon.listenserver()

        # Start b3270.
        b3270 = Popen(['b3270', '-json', '-callback', str(l.port)],
            stdin=DEVNULL, stdout=DEVNULL, stderr=DEVNULL)
        self.children.append(b3270)

        # Wait for it to call back.
        l.accept(timeout=2)

        # Feed it multiple commands.
        l.send(b'{"run":{"actions":"set monoCase"}}\n')
        l.send(b'{"run":{"actions":"set monoCase"}}\n')
        l.send(b'{"run":{"actions":"set monoCase"}}\n')

        # Grab its output.
        if sys.platform.startswith('win'):
            time.sleep(0.1)
        out = l.data(timeout=2).decode('utf8').split('\n')
        self.assertEqual(5, len(out))
        self.assertTrue(out[1].startswith('{"run-result":{'))
        self.assertTrue(out[2].startswith('{"run-result":{'))
        self.assertTrue(out[3].startswith('{"run-result":{'))
        self.assertEqual('', out[4])

        rc = b3270.wait(timeout=2)
        self.assertEqual(0, rc)

if __name__ == '__main__':
    unittest.main()
