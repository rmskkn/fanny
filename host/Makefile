# SPDX-License-Identifier: GPL-3.0-only
# Host-side Python unit tests for fanctl.py/fancontrold.py/fwtool.py, under
# tests/ (needs its own __init__.py: without one, unittest's discovery
# silently skips a subdirectory instead of descending into it). No
# hardware needed: tests fake the serial port / board connection.
#
#   make test    run with unittest (stdlib, no extra test dependency)

test:
	python3 -m unittest discover -s . -p 'test_*.py' -v

.PHONY: test
