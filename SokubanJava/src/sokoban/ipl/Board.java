package sokoban.ipl;

import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Scanner;

import ibis.ipl.ReadMessage;
import ibis.ipl.WriteMessage;

/**
 * Class representing a particular state of the Sokoban board.
 */
public class Board {

	/* directions in which the player can move */
	static int[][] DIRS = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };

	String curBoard; /* string representation of the current board (contains only the player and the boxes) */
	String destBoard; /* string representation of the final board (contains only the goals) */
	int playerX, playerY; /* coordinates of the player */
	int width; /* width of the board */
	int bound;
	int moves;
	byte[] signature = new byte[128];
	int sigiter = 0;

	public Board(String fileName) throws FileNotFoundException {
		String[] board = loadBoard(fileName);
		init(board);
	}
	
	public Board(Board board) {
		init(board);
	}
	
	
	
	/* (non-Javadoc)
	 * @see java.lang.Object#hashCode()
	 */
	@Override
	public int hashCode() {
		final int prime = 31;
		int result = 1;
		result = prime * result + ((curBoard == null) ? 0 : curBoard.hashCode());
		result = prime * result + moves;
		result = prime * result + sigiter;
		result = prime * result + Arrays.hashCode(signature);
		return result;
	}

	/* (non-Javadoc)
	 * @see java.lang.Object#equals(java.lang.Object)
	 */
	@Override
	public boolean equals(Object obj) {
		if (this == obj)
			return true;
		if (obj == null)
			return false;
		if (getClass() != obj.getClass())
			return false;
		Board other = (Board) obj;
		if (curBoard == null) {
			if (other.curBoard != null)
				return false;
		} else if (!curBoard.equals(other.curBoard))
			return false;
		if (moves != other.moves)
			return false;
		if (sigiter != other.sigiter)
			return false;
		if (!Arrays.equals(signature, other.signature))
			return false;
		return true;
	}
	
	public Board(ReadMessage reader) throws IOException {
		curBoard = reader.readString();
		playerX = reader.readInt();
		playerY = reader.readInt();
		destBoard = reader.readString();
		moves = reader.readInt();
		bound = reader.readInt();
		width = reader.readInt();
		reader.readArray(signature);
		sigiter = reader.readInt();
	}
	
	public void fillMessage(WriteMessage writer) throws IOException {
		writer.writeString(curBoard);
		writer.writeInt(playerX);
		writer.writeInt(playerY);
		writer.writeString(destBoard);
		writer.writeInt(moves);
		writer.writeInt(bound);
		writer.writeInt(width);
		writer.writeArray(signature);
		writer.writeInt(sigiter);
	}
	
	public void init(Board board) {
		curBoard = board.curBoard;
		playerX = board.playerX;
		playerY = board.playerY;
		destBoard = board.destBoard;
		moves = board.moves;
		bound = board.bound;
		width = board.width;
		signature = board.signature.clone();
		sigiter = board.sigiter;
	}
	
	/*
	 * read the initial configuration of the board from a file
	 * 
	 * ATTENTION: this method doesn't check if the file contains a valid
	 * board, so please double-check that the boards you provide as input
	 * are valid
	 */
	private static String[] loadBoard(String fileName) throws FileNotFoundException {
		Scanner scanner = new Scanner(new FileInputStream(fileName));
		ArrayList<String> board = new ArrayList<String>();

		while(scanner.hasNextLine()) {
			String row = scanner.nextLine();
			board.add(row);
		}
		
		scanner.close();
		return board.toArray(new String[0]);
	}

	/* initialize the board */
	private void init(String[] board) {
		width = board[0].length();
		StringBuilder destBuf = new StringBuilder();
		StringBuilder currBuf = new StringBuilder();

		for (int r = 0; r < board.length; r++) {
			for (int c = 0; c < width; c++) {

				char ch = board[r].charAt(c);

				if(ch == '+') {
					destBuf.append(".");
					currBuf.append("@");
				} else if(ch == '*') {
					destBuf.append(".");
					currBuf.append("$");
				} else {
					destBuf.append(ch != '$' && ch != '@' ? ch : ' ');
					currBuf.append(ch != '.' ? ch : ' ');
				}

				if (ch == '@' || ch == '+') {
					playerX = c;
					playerY = r;
				}
			}
		}
		destBoard = destBuf.toString();
		curBoard = currBuf.toString();
	}
	
	/* check if the game is solved */
	boolean isSolved() {
		for (int i = 0; i < curBoard.length(); i++)
			if ((destBoard.charAt(i) == '.') != (curBoard.charAt(i) == '$'))
				return false;
		return true;
	}

	/* push a box on the direction specified by dx and dy */
	private boolean push(int dx, int dy) {
		int newBoxPos = (playerY + 2 * dy) * width + playerX + 2 * dx;

		if (curBoard.charAt(newBoxPos) != ' ')
			return false;

		char[] trial = curBoard.toCharArray();
		trial[playerY * width + playerX] = ' ';
		trial[(playerY + dy) * width + playerX + dx] = '@';
		trial[newBoxPos] = '$';

		curBoard = new String(trial);
		playerX += dx;
		playerY += dy;
		moves++;
		
		return true;
	}
	
	/* move the player in the direction specified by dx and dy */
	private boolean move(int dx, int dy) {
		int newPlayerPos = (playerY + dy) * width + playerX + dx;

		if (curBoard.charAt(newPlayerPos) != ' ')
			return false;

		char[] trial = curBoard.toCharArray();
		trial[playerY * width + playerX] = ' ';
		trial[newPlayerPos] = '@';

		curBoard = new String(trial);
		playerX += dx;
		playerY += dy;
		moves++;
		return true;
	}
	
	/**
	 * Make all possible moves with this board position.
	 */
	public List<Board> generateChildren(BoardCache cache) {
		ArrayList<Board> children = new ArrayList<Board>();
		
		for (int i = 0; i < DIRS.length; i++) {
			Board child = cache.get(this);
			child.signature[child.sigiter] = (byte) ( child.playerX%256);
			child.sigiter++;
			child.sigiter%=child.signature.length;
			child.signature[child.sigiter] = (byte) ( child.playerY%256);
			child.sigiter++;
			child.sigiter%=child.signature.length;
			int dx = DIRS[i][0];
			int dy = DIRS[i][1];
			
			// are we standing next to a box ?
			if (curBoard.charAt((playerY + dy) * width + playerX + dx) == '$') {
				// can we push it ?
				if (child.push(dx, dy)) {
					children.add(child);
				}
			// otherwise try changing position
			} else {
				// can we move ?
				if (child.move(dx, dy)) {
					children.add(child);
				}
			}
		}
		
		return children;
	}
	
	@Override
	public String toString() {
		String[] rows = curBoard.split("(?<=\\G.{" + width + "})");
		String result = "";
		
		for(String row : rows) {
			result += row + "\n";
		}
		
		return result;
	}
	
	public int getBound() {
		return bound;
	}

	public void setBound(int bound) {
		this.bound = bound;
	}

	public int getMoves() {
		return moves;
	}

	public void setMoves(int moves) {
		this.moves = moves;
	}

//	@Override
//	public void generated_DefaultReadObject(IbisSerializationInputStream reader, int lvl)
//			throws IOException, ClassNotFoundException {
//		curBoard = reader.readString();
//		playerX = reader.readInt();
//		playerY = reader.readInt();
//		destBoard = reader.readString();
//		moves = reader.readInt();
//		bound = reader.readInt();
//		width = reader.readInt();
//		
//	}
//
//	@Override
//	public void generated_DefaultWriteObject(IbisSerializationOutputStream writer, int lvl) throws IOException {
//		writer.writeString(curBoard);
//		writer.writeInt(playerX);
//		writer.writeInt(playerY);
//		writer.writeString(destBoard);
//		writer.writeInt(moves);
//		writer.writeInt(bound);
//		writer.writeInt(width);
//	}
//
//	@Override
//	public void generated_WriteObject(IbisSerializationOutputStream writer) throws IOException {
//		writer.writeString(curBoard);
//		writer.writeInt(playerX);
//		writer.writeInt(playerY);
//		writer.writeString(destBoard);
//		writer.writeInt(moves);
//		writer.writeInt(bound);
//		writer.writeInt(width);
//		
//	}
}
