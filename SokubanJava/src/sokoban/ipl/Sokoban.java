/**
 * 
 */
package sokoban.ipl;

import java.io.IOException;
import java.util.ArrayList;

import ibis.ipl.IbisCreationFailedException;

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
		
		try {
			ipl = new IkoPanLayer(fileName);
		} catch (IbisCreationFailedException | IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} 
		
		//Initializes Ibis and votes for a starter
		int ibises = ipl.getIbisCount(); //recvs the detected ibis count
		
		ipl.run();
		
		System.out.println(ipl.finalSolutionCount);
		
		ipl.exit();
		
		System.exit(0);
	}

}
