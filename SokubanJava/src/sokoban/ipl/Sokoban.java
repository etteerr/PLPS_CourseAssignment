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
		
		try {
			ipl = new IkoPanLayer();
			ipl.initJobs(new Board("tests/t1.txt"));
		} catch (Exception e1) {
			// TODO Auto-generated catch block
			e1.printStackTrace();
			System.exit(1);
		}
		
		try {
			ipl.run();
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
