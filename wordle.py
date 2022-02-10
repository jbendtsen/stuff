#!/bin/python

import random

with open("5-letter-words.txt") as f:
	lines = f.read().splitlines()

word = lines[random.randint(0, len(lines)-1)]

i = 1
while i <= 6:
	# move back 5 positions, get user input
	print("{}/6: _____\x1b[5D".format(i), end="")
	reply = input()

	valid = False
	if len(reply) == 5:
		valid = True
		for c in reply:
			# ensure each character is between A-Z or a-z
			if not (ord(c) >= 0x41 and ord(c) <= 0x5a) and not (ord(c) >= 0x61 and ord(c) <= 0x7a):
				valid = False
				break

	# move up one line, delete line contents, move to start of line
	print("\x1b[1A\x1b[2K\x1b[1G", end="")

	if not valid:
		continue

	lower = reply.lower()
	output = "{}/6: ".format(i)
	n_correct = 0

	# build the output, which is just the input but with hints formatted as terminal colours
	for j in range(0, 5):
		code = "\x1b[40m"
		if word[j] == lower[j]:
			n_correct += 1
			code = "\x1b[42m"
		elif lower[j] in word:
			code = "\x1b[43m"

		output += code + reply[j]

	print(output + "\x1b[0m")

	if n_correct == 5:
		break

	i += 1

print(word)
