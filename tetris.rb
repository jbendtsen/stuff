# Tetris in Ruby with Gosu. I made this in October 2018 at Swinburne Uni as part of my High Distinction custom project.
# The main limitation was that writing code in an object-oriented fashion was strongly discouraged, so that led to some "interesting" workarounds.
# A self-imposed limitation was that I didn't want any external assets, all data was to be generated from code or be part of a lookup-table.
# At the time, the only way I knew how to render an image was to make a class that acted as a kind of interface to Gosu that I could pass a raw bitmap into.
# Thankfully, at the demonstration, my tutors were kind enough to look past the block rendering code as an artefact of using Ruby with Gosu.
# The Tetris logo was created with a basic tool I wrote (also using Gosu) for placing 2d quads on a grid and saving the result as a text file.
# As for the actual game, I based the scoring system off the NES version, but everything else I made up as I went along.

require 'gosu'

module Mode
	MENU, GAME = *0..1
end

class ImageFactory
	attr_reader :columns, :rows

	def initialize(cols, rows, blob)
		@columns = cols
		@rows = rows
		@blob = blob
	end

	def press
		Gosu::Image.new(self)
	end

	def to_blob
		@blob
	end
end

def pixel_layer(x, y, size)
	centre = size / 2
	x_off = (centre - x).abs
	y_off = (centre - y).abs

	layer = x_off > y_off ? x : y
	if layer > centre
		layer = size - layer - 1
	end

	layer
end

def create_tile(size, colour)
	a = ((colour >> 24) & 0xff)
	r = ((colour >> 16) & 0xff)
	g = ((colour >> 8) & 0xff)
	b = (colour & 0xff)
	cl = r.chr + g.chr + b.chr + a.chr

	shade = (size / 4).to_i
	grad = (256 / shade).to_i

	tile = "#{cl * size * size}"
	for i in 0..size-1
		for j in 0..size-1
			layer = pixel_layer(j, i, size)
			if layer < shade
				lum = layer * grad
				pr = (r * lum) / 256
				pg = (g * lum) / 256
				pb = (b * lum) / 256

				off = (i * size + j) * 4
				tile[off..off+3] = pr.chr + pg.chr + pb.chr + 255.chr
			end
		end
	end

	ImageFactory.new(size, size, tile).press
end

class TetrisGame < Gosu::Window
	# Who needs a global variable when you can have a class method instead, right?
	def get_piece(idx)
		p = nil

		case idx
		when 0 # O shape
			p = [
				[1, 1, 0, 0],
				[1, 1, 0, 0],
				[0, 0, 0, 0],
				[0, 0, 0, 0]
			]
		when 1 # J shape
			p = [
				[2, 2, 0, 0],
				[2, 0, 0, 0],
				[2, 0, 0, 0],
				[0, 0, 0, 0]
			]
		when 2 # L shape
			p = [
				[3, 3, 0, 0],
				[0, 3, 0, 0],
				[0, 3, 0, 0],
				[0, 0, 0, 0]
			]
		when 3 # Z shape
			p = [
				[0, 2, 0, 0],
				[2, 2, 0, 0],
				[2, 0, 0, 0],
				[0, 0, 0, 0]
			]
		when 4 # S shape
			p = [
				[3, 0, 0, 0],
				[3, 3, 0, 0],
				[0, 3, 0, 0],
				[0, 0, 0, 0],
			]
		when 5 # T shape
			p = [
				[0, 1, 0, 0],
				[1, 1, 0, 0],
				[0, 1, 0, 0],
				[0, 0, 0, 0]
			]
		when 6 # I shape
			p = [
				[1, 0, 0, 0],
				[1, 0, 0, 0],
				[1, 0, 0, 0],
				[1, 0, 0, 0]
			]
		end

		p
	end

	def bonus(n_cleared)
		score = 0

		case n_cleared
		when 1
			score = 40
		when 2
			score = 100
		when 3
			score = 300
		when 4
			score = 1200
		end

		score
	end

	def input_pause
		Gosu::KB_RETURN
	end
	def input_clockwise
		Gosu::KB_X
	end
	def input_anticlockwise
		Gosu::KB_Z
	end
	def input_move_right
		Gosu::KB_RIGHT
	end
	def input_move_left
		Gosu::KB_LEFT
	end
	def input_drop
		Gosu::KB_DOWN
	end
	def input_cheat
		Gosu::KB_UP
	end

	def move_rate
		6
	end
	def move_wait
		24
	end

	def default_rate
		# super arbitrary
		inc = (@level * @level / 8).to_i
		(2000 / (40 + (@level * 10) + inc)).to_i
	end
	def drop_rate
		4
	end

	def unit
		35
	end
	def n_rows
		20
	end
	def n_cols
		10
	end

	def curtain_speed
		(unit / 5).to_i
	end

	def left_margin
		4 * unit
	end
	def right_margin
		5 * unit
	end

	def width
		left_margin + (unit * n_cols) + right_margin
	end
	def height
		unit * n_rows
	end

	def font_size
		((unit * 3) / 4).to_i
	end

	def info_x
		unit
	end
	def ptext_y
		unit * 3
	end
	def points_y
		ptext_y + font_size
	end
	def ltext_y
		unit * 5
	end
	def level_y
		ltext_y + font_size
	end

	def next_x
		width - right_margin + unit
	end
	def ntext_y
		unit * 3
	end
	def next_y
		ntext_y + font_size
	end
	def npiece_x
		next_x + (unit / 2).to_i
	end
	def npiece_y
		next_y + (unit / 2).to_i
	end

	def paused_size
		unit * 5
	end

	def initialize
		# A series of quads that present the Tetris logo
		@logo = [
			[0.795, 0.305, 0.81, 0.315, 0.795, 0.37, 0.81, 0.37],
			[0.81, 0.315, 0.95, 0.315, 0.81, 0.37, 0.95, 0.37],
			[0.885, 0.305, 0.95, 0.305, 0.89, 0.315, 0.95, 0.315],
			[0.795, 0.14, 0.86, 0.14, 0.885, 0.305, 0.95, 0.305],
			[0.795, 0.135, 0.855, 0.135, 0.795, 0.14, 0.86, 0.14],
			[0.915, 0.09, 0.945, 0.09, 0.915, 0.135, 0.925, 0.15],
			[0.795, 0.09, 0.915, 0.09, 0.795, 0.135, 0.915, 0.135],
			[0.725, 0.15, 0.785, 0.15, 0.725, 0.26, 0.785, 0.36],
			[0.59, 0.18, 0.67, 0.18, 0.705, 0.37, 0.785, 0.37],
			[0.635, 0.125, 0.675, 0.125, 0.59, 0.18, 0.635, 0.18],
			[0.725, 0.09, 0.785, 0.09, 0.725, 0.14, 0.785, 0.14],
			[0.59, 0.09, 0.7, 0.09, 0.59, 0.125, 0.675, 0.125],
			[0.535, 0.09, 0.59, 0.09, 0.535, 0.37, 0.59, 0.37],
			[0.365, 0.09, 0.53, 0.09, 0.365, 0.14, 0.53, 0.14],
			[0.415, 0.14, 0.475, 0.14, 0.415, 0.37, 0.475, 0.37],
			[0.27, 0.31, 0.375, 0.31, 0.27, 0.37, 0.41, 0.37],
			[0.27, 0.175, 0.335, 0.175, 0.27, 0.22, 0.3, 0.22],
			[0.27, 0.09, 0.36, 0.09, 0.27, 0.14, 0.33, 0.14],
			[0.21, 0.09, 0.27, 0.09, 0.21, 0.37, 0.27, 0.37],
			[0.09, 0.14, 0.15, 0.14, 0.09, 0.37, 0.15, 0.37],
			[0.035, 0.09, 0.205, 0.09, 0.035, 0.14, 0.205, 0.14]
		]

		super(width, height, false)
		self.caption = "Tetris"

		@font = Gosu::Font.new(font_size)
		@paused_font = Gosu::Font.new(paused_size)

		@palette = [0, 1, 2]
		@colours = [
			0xffff0000, # Red
			0xff00ff00, # Green
			0xff0000ff, # Blue
			0xff00ffff, # Cyan
			0xffff00ff, # Magenta
			0xffffff00, # Yellow
			0xffff8000, # Orange
			0xff8000ff, # Purple
			0xff00ff80, # Turquoise
			0xff808080, # Grey
			0xffffffff, # Whites
		]

		@board = Array.new(n_rows) { Array.new(n_cols) {0} }

		@tileset = []
		@colours.each { |colour|
			@tileset << create_tile(unit, colour)
		}

		@mode = Mode::MENU
		@paused = false

		reset_board(false)
	end

	def reset_board(erase)
		if erase
			@board.length.times do |i|
				@board[i].fill(0)
			end
		end

		@demo_hold = 0
		@demo_next = 0

		@game_over = false
		@curtain = nil
		@end_timer = nil

		@level = 0
		@cleared = 0
		@points = 0

		@drop_timer = 0
		@dropped = 0
		@move_timer = 0
		@move_dir = 0

		@fall_rate = default_rate
		@fall_dir = 1

		@next_piece_idx = rand(7) # 7 pieces in Tetris
		next_piece()
	end

	def generate_palette
		set = []
		@colours.each_with_index { |cl, idx| set << idx }

		for i in 0..3
			idx = rand(set.length)
			@palette[i] = set[idx]
			set.delete_at(idx)
		end
	end

	def calc_piece_edges(axis)
		left = -1
		right = -1

		for i in 0..3
			# loop unrolling ftw
			if axis == 0
				empty = (@piece[0][i] != 0 or @piece[1][i] != 0 or @piece[2][i] != 0 or @piece[3][i] != 0)
			else
				empty = (@piece[i][0] != 0 or @piece[i][1] != 0 or @piece[i][2] != 0 or @piece[i][3] != 0)
			end

			if empty
				if left == -1
					left = i
				else
					right = i
				end
			end
		end

		if left == -1
			left = 0
			if right == -1
				right = 3
			end
		elsif right == -1
			right = left
		end

		[left, right]
	end

	def next_piece
		if @game_over
			return
		end

		@piece_idx = @next_piece_idx
		@next_piece_idx = rand(7) # 7 pieces in Tetris
		@next_piece = get_piece(@next_piece_idx)

		@piece_dir = 0
		@col = 4
		@row = 0

		# if it's an I shape, make it horizontal (because I'm nice like that)
		rotate_piece(@piece_idx == 6 ? 1 : 0)
		@col = (n_cols - @piece_w) / 2

		for i in 0..@piece_h
			for j in 0..@piece_w
				if @board[@row+i][@col+j] != 0 and @piece[i][j] != 0
					@game_over = true
				end
			end
		end
	end

	def rotate_piece(dir)
		@piece_dir = ((@piece_dir + dir) + 4) % 4
		rot = Array.new(4) { Array.new(4) {0} }

		# Rotate the piece
		#   12:00: x -> x, y -> y
		#   03:00: x -> -y, y -> x
		#   06:00: x -> -x, y -> -y
		#   09:00: x -> y, y -> -x

		p = get_piece(@piece_idx)
		for i in 0..3
			for j in 0..3
				x = j
				y = i
				if @piece_dir == 1
					x = 3-i
					y = j
				elsif @piece_dir == 2
					x = 3-j
					y = 3-i
				elsif @piece_dir == 3
					x = i
					y = 3-j
				end

				rot[y][x] = p[i][j]
			end
		end

		# Move the piece inside its box so that it occupies the left-most position
		for i in 0..3
			if rot[0][0] == 0 and rot[1][0] == 0 and rot[2][0] == 0 and rot[3][0] == 0
				for j in 0..3
					rot[j][0] = rot[j][1]
					rot[j][1] = rot[j][2]
					rot[j][2] = rot[j][3]
					rot[j][3] = 0
				end
			else
				break
			end
		end

		# Move the piece inside its box so that it occupies the top-most position
		for i in 0..3
			if rot[0][0] == 0 and rot[0][1] == 0 and rot[0][2] == 0 and rot[0][3] == 0
				for j in 0..3
					rot[0][j] = rot[1][j]
					rot[1][j] = rot[2][j]
					rot[2][j] = rot[3][j]
					rot[3][j] = 0
				end
			else
				break
			end
		end

		i = 3
		while i >= 0 and rot[0][i] == 0 and rot[1][i] == 0 and rot[2][i] == 0 and rot[3][i] == 0
			i -= 1
		end
		w_new = i + 1

		i = 3
		while i >= 0 and rot[i][0] == 0 and rot[i][1] == 0 and rot[i][2] == 0 and rot[i][3] == 0
			i -= 1
		end
		h_new = i + 1

		row_new = @row
		col_new = @col
		if @col + w_new > n_cols
			col_new = n_cols - w_new
		end

		# Test if the piece once rotated will overlap with the board
		overlap = false
		if dir != 0
			for i in 0..3
				for j in 0..3
					if rot[i][j] != 0 and (row_new + i >= n_rows or @board[row_new+i][col_new+j] != 0)
						overlap = true
						break
					end
				end
				if overlap
					break
				end
			end
		end

		if !overlap
			@piece = rot
			@piece_w = w_new
			@piece_h = h_new
			@row = row_new
			@col = col_new
		end
	end

	def check_collision
		@left_wall = false
		@right_wall = false
		@bottom_wall = false

		for i in 0..3
			if @row+i < 0
				next
			end

			for j in 0..3
				if @piece[i][j] <= 0
					next
				end

				if @col+j > 0 and @board[@row+i][@col+j-1] > 0
					@left_wall = true
				end
				if @col+j < n_cols-1 and @board[@row+i][@col+j+1] > 0
					@right_wall = true
				end
				if @row+i < n_rows-1 and @board[@row+i+1][@col+j] > 0
					@bottom_wall = true
				end
			end
		end
	end

	def bake_piece
		for i in 0..3
			for j in 0..3
				if @piece[i][j] > 0
					@board[@row+i][@col+j] = @piece[i][j]
				end
			end
		end
	end

	def clear_full_rows
		n_cleared = 0

		for i in 0..n_rows-1
			full = true
			for j in 0..n_cols-1
				if @board[i][j] <= 0
					full = false
					break
				end
			end

			unless full
				next
			end

			i.downto(1) { |r| @board[r].replace(@board[r-1]) }
			@board[0].fill(0)

			i -= 1
			n_cleared += 1
		end

		n_cleared
	end

	def demo_get_num(size)
		n = @demo_input % size
		@demo_input = (@demo_input / size).to_i
		n
	end

	def demo_next_input
		if @demo_input == nil
			@demo_input = rand(36000)
		end

		actions = [
			input_anticlockwise,
			input_clockwise,
			input_move_left,
			input_move_right,
			input_drop
		]

		@demo_action = actions[demo_get_num(5)]
		@demo_hold = demo_get_num(60) + 2
		@demo_next = demo_get_num(120) + 4

		if @demo_hold >= @demo_next
			@demo_hold = @demo_next - 2
		end

		button_down(@demo_action, true)
		@demo_input = rand(36000)
	end

	def update
		if @paused
			return
		end

		if @mode == Mode::MENU
			if @demo_hold == -1
				button_up(@demo_action, true)
			end
			if @demo_next == 0
				demo_next_input()
			end

			@demo_next -= 1
			@demo_hold -= 1
		end

		if @game_over
			if @curtain == nil
				@curtain = 0
			elsif @curtain != height
				@curtain += curtain_speed

				if @curtain > height
					@curtain = height
				end
			else
				if @end_timer == nil
					@end_timer = 0
				else
					@end_timer += 1
					if @mode == Mode::MENU and @end_timer >= 120
						reset_board(true)
					end
				end
			end

			return
		end

		check_collision()
		ready = (@move_timer == 0 or ((@move_timer % move_rate) == 0 and @move_timer > move_wait))

		if ready and ((!@left_wall and @move_dir < 0) or (!@right_wall and @move_dir > 0))
			@col += @move_dir
		end

		if @col < 0
			@col = 0
		end
		if @col > n_cols - @piece_w
			@col = n_cols - @piece_w
		end

		landed = false
		if @drop_timer == @fall_rate-1
			@row += @fall_dir
			if @drop
				@dropped += 1
			end

			@drop_timer = 0
			if @bottom_wall
				landed = true
			end
		end

		if @row > n_rows-@piece_h or landed
			@row -= 1
			bake_piece()

			rows = clear_full_rows()
			@cleared += rows

			next_level = (@cleared / 10).to_i
			if next_level > @level
				@level = next_level
				generate_palette()
			end

			@points += bonus(rows) * (@level + 1)
			@points += @dropped
			@dropped = 0

			next_piece()
			@fall_rate = default_rate
		end

		@move_timer += 1
		@drop_timer += 1
	end

	def draw_cell(idx, x, y)
		if idx > 0 and idx <= 3
			@tileset[@palette[idx-1]].draw(x, y, 0)
		end
	end

	def draw
		if @mode == Mode::GAME and @paused
			@paused_font.draw_rel("PAUSED", width/2, height/2, 0, 0.5, 0.5)
			return
		end

		back_cl = 0xff404040
		draw_rect(0, 0, left_margin, height, back_cl)
		draw_rect(width-right_margin, 0, right_margin, height, back_cl)

		for i in 0..3
			for j in 0..3
				draw_cell(@piece[i][j], left_margin + (@col + j) * unit, (@row + i) * unit)
			end
		end

		@board.each_with_index { |row, r_idx|
			row.each_with_index { |cell, c_idx|
				draw_cell(cell, left_margin + c_idx * unit, r_idx * unit)
			}
		}

		draw_rect(next_x, next_y, unit * 3, unit * 5, 0xff202020)
		for i in 0..3
			for j in 0..1
				draw_cell(@next_piece[i][j], npiece_x + j * unit, npiece_y + i * unit)
			end
		end

		@font.draw("Points", info_x, ptext_y, 0)
		@font.draw("#{@points}", info_x, points_y, 0)
		@font.draw("Level", info_x, ltext_y, 0)
		@font.draw("#{@level}", info_x, level_y, 0)

		if @curtain != nil
			draw_rect(left_margin, 0, unit * n_cols, @curtain, 0xff800000)
		end

		if @mode == Mode::MENU
			draw_rect(0, 0, width, height, 0x60000000)

			cl = 0xffe0e0e0
			@logo.each { |q|
				x1 = (q[0] * width.to_f).to_i
				y1 = (q[1] * height.to_f).to_i
				x2 = (q[2] * width.to_f).to_i
				y2 = (q[3] * height.to_f).to_i
				x3 = (q[4] * width.to_f).to_i
				y3 = (q[5] * height.to_f).to_i
				x4 = (q[6] * width.to_f).to_i
				y4 = (q[7] * height.to_f).to_i

				draw_quad(x1, y1, cl, x2, y2, cl, x3, y3, cl, x4, y4, cl, 0)
			}
		end

	end

	def move_piece(dir)
		@move_dir = @move_dir == 0 ? dir : 0
		@move_timer = 0
	end

	def button_down(id, demo = false)
		if @mode == Mode::MENU and demo == false
			if id == input_pause
				@mode = Mode::GAME
				reset_board(true)
			end
			return
		end

		case id
		when input_anticlockwise
			rotate_piece(-1)
		when input_clockwise
			rotate_piece(1)
		when input_move_left
			move_piece(-1)
		when input_move_right
			move_piece(1)
		when input_drop
			@drop = true
			@fall_rate = drop_rate
			@drop_timer = @fall_rate-1
=begin
		when input_cheat # ;)
			@fall_dir = -1
			@drop_timer = @fall_rate-1
=end
		when input_pause
			if !@game_over
				@paused = !@paused
			elsif @end_timer != nil
				reset_board(true)
				@mode = Mode::MENU
			end
		end
	end

	def button_up(id, demo = false)
		if @mode == Mode::MENU and demo == false
			return
		end

		case id
		when input_move_left
			move_piece(0)
		when input_move_right
			move_piece(0)
		when input_drop
			@drop = false
			@fall_rate = default_rate
			@dropped = 0
=begin
		when input_cheat # ;)
			@fall_dir = 1
=end
		end
	end
end

TetrisGame.new.show
