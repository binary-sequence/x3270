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
# b3270 XML pass-through tests

import unittest
from subprocess import Popen, PIPE, DEVNULL
import select
import xml.etree.ElementTree as ET
import TestCommon

class TestB3270PassthruXml(unittest.TestCase):

    # Set up procedure.
    def setUp(self):
        self.children = []

    # Tear-down procedure.
    def tearDown(self):
        # Tidy up the children.
        for child in self.children:
            child.kill()
            child.wait()

    # b3270 passthru XML test
    def test_b3270_passthru_xml(self):

        b3270 = Popen(['b3270'], stdin=PIPE, stdout=PIPE)
        self.children.append(b3270)

        # Get the initial dump.
        while True:
            line = b3270.stdout.readline().decode('utf8')
            if '</initialize>' in line:
                break

        # Register the Foo action and run it.
        top = ET.Element('b3270-in')
        ET.SubElement(top, 'register', { 'name': 'Foo' })
        ET.SubElement(top, 'run', { 'r-tag': 'abc', 'actions': "Foo(a,b)" })
        *first, _, _ = TestCommon.xml_prettify(top).split(b'\n')
        b3270.stdin.write(b'\n'.join(first) + b'\n')
        b3270.stdin.flush()

        # Get the result.
        r, _, _ = select.select([ b3270.stdout.fileno() ], [], [], 2)
        self.assertNotEqual([], r)
        s = b3270.stdout.readline().decode('utf8')
        out = ET.fromstring(s)
        self.assertEqual('passthru', out.tag)
        attr = out.attrib
        tag = attr['p-tag']
        self.assertEqual('abc', attr['parent-r-tag'])
        self.assertEqual('a', attr['arg1'])
        self.assertEqual('b', attr['arg2'])
        self.assertFalse('arg3' in attr)

        # Make the action succeed.
        succeed = ET.Element('succeed', { 'p-tag': tag, 'text': 'hello\nthere' })
        b3270.stdin.write(ET.tostring(succeed) + b'\n')
        b3270.stdin.flush()

        # Get the result of that.
        r, _, _ = select.select([ b3270.stdout.fileno() ], [], [], 2)
        self.assertNotEqual([], r)
        s = b3270.stdout.readline().decode('utf8')
        out = ET.fromstring(s)
        self.assertEqual('run-result', out.tag)
        attr = out.attrib
        self.assertEqual('abc', attr['r-tag'])
        self.assertEqual('true', attr['success'])
        self.assertEqual('hello\nthere', attr['text'])

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.stdout.close()
        b3270.wait(timeout=2)

if __name__ == '__main__':
    unittest.main()
