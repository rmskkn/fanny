# SPDX-License-Identifier: GPL-3.0-only
# Host-side Python unit tests for fanctl.py/fancontrold.py. No hardware
# needed: tests fake the serial port / board connection.
#
#   make test    run with unittest (stdlib, no extra test dependency)

test:
	python3 -m unittest discover -s . -p 'test_*.py' -v

.PHONY: test
