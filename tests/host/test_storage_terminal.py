#!/usr/bin/env python3
"""Regressions for reading the real terminal without replaying old prompts."""
import struct
import socket
from unittest.mock import patch
import unittest

from test_storage_boot import QMPChannel, TerminalTranscript


def snapshot(lines, cursor=None):
    raw = bytearray(72 + 100 * 80 * 2)
    for row in range(100):
        text = lines[row] if row < len(lines) else ''
        if len(text) > 80:
            raise ValueError('fixture row exceeds terminal width')
        for col, char in enumerate(text.ljust(80)):
            struct.pack_into('<H', raw, 72 + (row * 80 + col) * 2, ord(char) | 0x0700)
    x, y = cursor or (len(lines[-1]), len(lines) - 1)
    struct.pack_into('<II', raw, 0, x, y)
    return raw


class TerminalTests(unittest.TestCase):
    def test_login_starts_after_init_clears_the_boot_console(self):
        terminal = TerminalTranscript(wait_for_login=True)
        self.assertEqual(terminal.update(snapshot(['Boot process completed.'])), '')
        self.assertEqual(terminal.update(snapshot([''])), '')
        self.assertEqual(terminal.update(snapshot(['Login Screen', ''])), '')
        result = terminal.update(snapshot(['Login Screen', '', 'login: ']))
        self.assertEqual(result, 'Login Screen\n\nlogin: ')
        self.assertEqual(terminal.update(snapshot(['Login Screen', '', 'login: root'])),
                         'Login Screen\n\nlogin: root')

    def test_cursor_redraw_cannot_replay_prompt(self):
        terminal = TerminalTranscript()
        prompt = 'root@esdumanOS /root # '
        terminal.update(snapshot([prompt]))
        terminal.update(snapshot([prompt + 'echo status-2 ']))
        before = terminal.text
        self.assertEqual(terminal.update(snapshot([prompt + 'echo status-2 '], (0, 0))), before)
        result = terminal.update(snapshot([prompt + 'echo status-2 $?', 'status-2 0', '', prompt]))
        self.assertEqual(result, '\n' + prompt + 'echo status-2 $?\nstatus-2 0\n\n' + prompt)
        self.assertEqual(result.count(prompt), 2)

    def test_scrollback_appends_only_new_output(self):
        terminal = TerminalTranscript()
        prompt = 'root@esdumanOS /root # '
        old = [f'old line {i}' for i in range(99)] + [prompt]
        terminal.update(snapshot(old))
        before = terminal.text
        new = old[3:-1] + [prompt + 'echo status-3 $?', 'status-3 1', '', prompt]
        result = terminal.update(snapshot(new))
        self.assertEqual(result[len(before):], 'echo status-3 $?\nstatus-3 1\n\n' + prompt)
        self.assertNotIn('old line', result[len(before):])

    def test_cursor_on_an_older_row_is_not_new_output(self):
        terminal = TerminalTranscript()
        lines = ['output', 'root@esdumanOS / # echo']
        before = terminal.update(snapshot(lines))
        self.assertEqual(terminal.update(snapshot(lines, (0, 0))), before)
        self.assertEqual(terminal.update(snapshot(lines)), before)

    def test_unrelated_history_is_refused(self):
        terminal = TerminalTranscript()
        terminal.update(snapshot(['old history', 'prompt # ']))
        with self.assertRaises(RuntimeError):
            terminal.update(snapshot(['unrelated history', 'prompt # ']))


class FakeSocket:
    def __init__(self, frames):
        self.frames = iter([b'{"QMP": {}}\n', *frames])
        self.sent = []

    def settimeout(self, timeout):
        self.timeout = timeout

    def sendall(self, message):
        self.sent.append(message)

    def recv(self, size):
        frame = next(self.frames)
        if isinstance(frame, Exception):
            raise frame
        return frame


class QMPTests(unittest.TestCase):
    def test_delayed_fragment_preserved_and_command_sent_once(self):
        connection = FakeSocket([b'{"event":"STOP"}\n{"ret', socket.timeout(),
                                 b'urn":{},"id":1}\n'])
        channel = QMPChannel(connection)
        self.assertEqual(channel.command('stop'), {})
        self.assertEqual(len(connection.sent), 1)

    def test_missing_reply_obeys_total_deadline(self):
        connection = FakeSocket([socket.timeout()])
        channel = QMPChannel(connection, timeout=2)
        with patch('test_storage_boot.time.monotonic', side_effect=[0, 0, 1, 3]):
            with self.assertRaises(TimeoutError):
                channel.command('query-status')
        self.assertEqual(len(connection.sent), 1)

    def test_disconnect_and_wrong_request_are_not_success(self):
        for frame in (b'', b'{"return":{},"id":99}\n'):
            with self.subTest(frame=frame):
                channel = QMPChannel(FakeSocket([frame]))
                with self.assertRaises(RuntimeError):
                    channel.command('query-status')

    def test_qmp_error_is_not_success(self):
        channel = QMPChannel(FakeSocket([b'{"error":{"desc":"failed"},"id":1}\n']))
        with self.assertRaisesRegex(RuntimeError, 'failed'):
            channel.command('human-monitor-command')


if __name__ == '__main__':
    unittest.main()
