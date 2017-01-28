/**
 * 
 */
package sokoban.ipl;

/**
 * @author Erwin Diepgrond
 *
 */
public class Sokoban {

	/**
	 * @param args should contain the path to the sokuban board file to be solved
	 */
	public static void main(String[] args) {
		IkoPanLayer ipl = null;
		String fileName = args[0];
		boolean serverOnly;
		serverOnly = false;
		if (args.length > 1)
			serverOnly = args[1].equals("serverOnly");
		
		try {
			ipl = new IkoPanLayer();
			ipl.initJobs(new Board(fileName));
		} catch (Exception e1) {
			// TODO Auto-generated catch block
			e1.printStackTrace();
			System.exit(1);
		}
		
		try {
			if (ipl.iAmServer) {
				System.out.print("Bound now:");
				long start = System.currentTimeMillis();
				ipl.run(serverOnly);
				long end = System.currentTimeMillis();
				System.err.println("Sokoban took " + (end - start) + " milliseconds");
	//			Bound now: 1 2 3 4 5 6 7 8 9 10 11 12
				int solutions = 0;
				if (ipl.getSolutions()!=null){
					for (Board i : ipl.getSolutions()) {
						if (i.getMoves() == ipl.bound)
							solutions++;
					}
					System.out.println("Solving game possible in "+solutions+" ways of "+ipl.bound+" steps");
		//			Solving game possible in 1 ways of 12 steps
				}
			}else {
				ipl.run();
			}
			ipl.shutdown();
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			ipl.shutdown();
			System.exit(2);
		}
		
		System.exit(0);
	}

}
