package sokoban.sequential;

import java.io.FileNotFoundException;
import java.util.List;

/**
 * @author Vladimir Bozdog
 *
 */
public class Sokoban {

	/* solves the game by incrementally increasing the bound */
	private static String solve(Board board) {
		BoardCache cache = new BoardCache();
		int bound = 0;
		int result = 0;

		System.out.print("Bound now:");

		while(result == 0) {
			bound++;
			board.setBound(bound);
			System.out.print(" " + bound);

			result = solutions(board, cache);
		}

		System.out.println();
        System.out.println("Solving game possible in " + result + " ways of " + bound + " steps");

		return null;
	}

	/**
	 * expands this board into all possible positions, and returns the number of
	 * solutions. Will cut off at the bound set in the board.
	 */
	private static int solutions(Board board, BoardCache cache) {
		int result = 0;

		if(board.isSolved()) {
			return 1;
		}

		if(board.getMoves() >= board.getBound()) {
			return 0;
		}

		List<Board> children = board.generateChildren(cache);

		for (Board child : children) {
            int childSolutions = solutions(child, cache);

            if (childSolutions > 0) {
                result += childSolutions;
            }

            cache.put(child);
        }

		return result;
	}

	public static void main(String[] args) {
		if(args.length == 0) {
			System.err.println("Input file not provided.");
			System.exit(1);
		}

		String fileName = args[0];

		try {
			Board board = new Board(fileName);
			System.out.println("Running Sokoban, initial board:\n" + board);

			long start = System.currentTimeMillis();
			solve(board);
			long end = System.currentTimeMillis();
			System.err.println("Sokoban took " + (end - start) + " milliseconds");
		} catch (FileNotFoundException e) {
			System.err.println("Input file not found.");
		}
	}


}
