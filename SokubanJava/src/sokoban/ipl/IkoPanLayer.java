package sokoban.ipl;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import ibis.ipl.ConnectionFailedException;
import ibis.ipl.Ibis;
import ibis.ipl.IbisCapabilities;
import ibis.ipl.IbisCreationFailedException;
import ibis.ipl.IbisFactory;
import ibis.ipl.IbisIdentifier;
import ibis.ipl.MessageUpcall;
import ibis.ipl.PortType;
import ibis.ipl.ReadMessage;
import ibis.ipl.ReceivePort;
import ibis.ipl.ReceivePortIdentifier;
import ibis.ipl.SendPort;
import ibis.ipl.WriteMessage;

public class IkoPanLayer implements MessageUpcall {

	private static final IbisCapabilities requiredCapabilities = new IbisCapabilities(IbisCapabilities.ELECTIONS_STRICT,
			IbisCapabilities.MEMBERSHIP_TOTALLY_ORDERED, IbisCapabilities.SIGNALS);

	private static final PortType broadcastPort = new PortType(PortType.CONNECTION_MANY_TO_MANY,
			PortType.RECEIVE_AUTO_UPCALLS, PortType.COMMUNICATION_RELIABLE, PortType.SERIALIZATION_DATA);

	private static final PortType receivePort = new PortType(PortType.CONNECTION_MANY_TO_ONE,
			PortType.RECEIVE_AUTO_UPCALLS, PortType.COMMUNICATION_RELIABLE, PortType.CONNECTION_DIRECT,
			PortType.SERIALIZATION_DATA);

	private static final PortType sendPort = new PortType(PortType.CONNECTION_ONE_TO_ONE,
			PortType.COMMUNICATION_RELIABLE, PortType.CONNECTION_DIRECT, PortType.SERIALIZATION_DATA);

	private static final int jobRequestTreshold = 40;

	private static final int jobRequestMaxReply = 5000;

	// Objects
	private IkoPanJobs jobs; // Stores all jobs
	private Ibis myIbis;
	private HashSet<Board> solutions;

	// Ports
	private ReceivePort broadcastReceive;

	private ReceivePort otmReceive;

	private SendPort otoSend;

	private SendPort broadcastSend;

	// The original, the loader of worlds
	private IbisIdentifier harbinger;

	private HashSet<IbisIdentifier> connectedTo;

	private boolean systemRunning;

	private BoardCache bc;

	// bcast reply expectator
	int bcastRepliesExpected = 0;
	
	//Final solution count
	int finalSolutionCount = 0;

	/**
	 * Specifies request type by byte
	 * 	Limited to 8 types to add redundancy (only 1 bit may be 1)
	 * 	This to reduce the possibility of request corruption
	 * @author erwin
	 *
	 */
	class MessageTypes {
		public static final byte jobRequest = 2^0;
		//public static final byte newShortest = 2^1; //Not used (these are signals)
		public static final byte gather = 2^2;
		public static final byte gatherReply = 2^3;
		public static final byte jobRequestReply = 2^4;
	}

	/**
	 * Initializes the IkoPanLayer Starts a ibis instance Votes for the right to
	 * start Initializes board
	 *
	 * @param fileName
	 *            Path to a board file
	 * @throws IbisCreationFailedException
	 *             Ibis creation failed
	 * @throws IOException
	 *             election failed
	 * @throws FileNotFoundException
	 *             board not found
	 */
	public IkoPanLayer(String fileName) throws IbisCreationFailedException, IOException, FileNotFoundException {
		myIbis = IbisFactory.createIbis(requiredCapabilities, // Capabilities
				null, // Registry handler
				broadcastPort, sendPort, receivePort); // Ports

		// Initialize ports
		broadcastReceive = myIbis.createReceivePort(broadcastPort, "bcast", this);
		broadcastReceive.enableConnections();
		broadcastSend = myIbis.createSendPort(broadcastPort, "bcast" + myIbis.identifier().name());
		otmReceive = myIbis.createReceivePort(receivePort, "recv" + myIbis.identifier().name(), this);
		otoSend = myIbis.createSendPort(sendPort, "send" + myIbis.identifier().name());

		// Enable downcalls

		// Select the one to load the board and start the process
		try {
			harbinger = myIbis.registry().elect("harbinger");
		} catch (IOException e) {
			myIbis.end();
			throw e;
		}

		// Initialize jobs & solutions
		bc = new BoardCache();
		jobs = new IkoPanJobs(bc);
		solutions = new HashSet<Board>();

		// Init variables
		connectedTo = new HashSet<IbisIdentifier>();

		// “We are superior.” ~Harbinger
		if (!harbinger.equals(myIbis.identifier()))
			return;

		// “I am Harbinger.” ~Harbinger
		Board b = new Board(fileName); // throws FileNotFound

		// Add job
		jobs.add(b);
	}

	public int getIbisCount() {
		return myIbis.registry().joinedIbises().length
				- (myIbis.registry().diedIbises().length + myIbis.registry().leftIbises().length);
	}

	public void run() {
		// Connecto to ibises
		connectToAllIbises();
		// Init run
		systemRunning = true;

		while (systemRunning) {
			jobStep();
			updateBoundLimit();
		}
	}

	private void updateBoundLimit() {
		String[] a = myIbis.registry().receivedSignals();
		for (String i : a)
			handleSignal(i);
	}

	private void connectToAllIbises() {

		for (IbisIdentifier i : myIbis.registry().joinedIbises()) {
			if (!connectedTo.contains(i) && !i.equals(myIbis.identifier())) {
				try {
					broadcastSend.connect(i, "bsend" + myIbis.identifier().name());
					connectedTo.add(i);
				} catch (ConnectionFailedException e) {
					try {
						myIbis.registry().assumeDead(i);
					} catch (IOException e1) {
						e.printStackTrace();
					}
				}
			}
		}

	}

	private void jobStep() {
		if (jobs.getnJobs() < jobRequestTreshold) {
			requestJobs();
		}

		Board b = jobs.get();

		if (b == null) {
			//while we are still waiting for jobs and jobs.get == null, sleep to release lock in jobs
			while(bcastRepliesExpected > 0 && (b=jobs.get())==null)
				try { Thread.sleep(50); } catch (InterruptedException e) {};
				
			if (b == null) {
				requestGather();
				while (bcastRepliesExpected > 0 );
				countOwnSolutions(jobs.getBoundLimit());
				systemRunning = false;
				return;
			}
			
		}

		java.util.List<Board> l = b.generateChildren(bc);

		// Sort solved and jobs
		for (Board i : l) {
			if (i.isSolved()) {
				solutions.add(i);
				bcastShortest(i.moves);
				jobs.setBoundLimit(i.moves);
			} else
				jobs.add(i);
		}
		
		if (b.isSolved()) {
			solutions.add(b);
			bcastShortest(b.moves);
			jobs.setBoundLimit(b.moves);
		}else
			bc.put(b);
	}

	private int countOwnSolutions(int boundLimit) {
		int isolut = 0;
		for (Board i : solutions)
			if (i.moves==boundLimit)
				isolut++;
		
		return isolut;
		
	}

	private void requestGather() {
		while (bcastRepliesExpected > 0);

		try {
			WriteMessage msg = broadcastSend.newMessage();
			msg.writeInt(MessageTypes.gather);
			msg.writeInt(jobs.getBoundLimit());
			msg.writeObject(otmReceive.identifier());
			msg.finish();
			bcastRepliesExpected = broadcastSend.connectedTo().length;
		} catch (IOException e) {
		}
	}

	/**
	 * bcasts message containing
	 * 	-message type
	 * 	-ReceivePortIdentifier
	 */
	private void requestJobs() {
		if (bcastRepliesExpected > 0)
			return;
		
		connectToAllIbises();
		
		if (connectedTo.isEmpty())
			return;
		
		try {
			WriteMessage msg = broadcastSend.newMessage();
			msg.writeInt(MessageTypes.jobRequest);
			msg.writeObject(otmReceive.identifier());
			msg.finish();
			bcastRepliesExpected = broadcastSend.connectedTo().length;
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	
	private void bcastShortest(int moves) {
		for (IbisIdentifier i : connectedTo)
			try {
				myIbis.registry().signal(Integer.toString(moves), i);
			} catch (IOException e) {
			}
	}

	@Override
	/**
	 * @param msg
	 * 	msg expects ateast:
	 * 		- Byte indicating MessageType
	 * 		- data corresponding to messageType
	 */
	public void upcall(ReadMessage msg) throws IOException, ClassNotFoundException {
		byte type = msg.readByte();
		
		switch(type) {
			case MessageTypes.jobRequest:
				handleJobRequest(msg);
				msg.finish();
				break;
			case MessageTypes.gather:
				handleGatherRequest(msg);
				msg.finish();
				break;
			case MessageTypes.gatherReply:
				handleGatherReply(msg);
				bcastRepliesExpected--;
				msg.finish();
				break;
			case MessageTypes.jobRequestReply:
				handeJobRequestReply(msg);
				bcastRepliesExpected--;
				msg.finish();
				break;
				
			default:
				//Wut?
		}
	}

	/**
	 * 
	 * @param msg
	 * msg should contain:
	 * - number of jobs send (n)
	 * - n Boards
	 * 
	 * synchronized is already in jobs object
	 */
	private void handeJobRequestReply(ReadMessage msg) {
		try {
			int jobsReplied = msg.readInt();
			Board b;
			for (int i = 0; i < jobsReplied; i++) {
				b = new Board(msg);
				jobs.add(b);
			}
		} catch (IOException e) {
			e.printStackTrace();
		}
		
	}

	/**
	 * 
	 * @param msg Only one int containing the solution count
	 */
	private synchronized void handleGatherReply(ReadMessage msg) {
		try {
			finalSolutionCount += msg.readInt();
		} catch (IOException e) {
			e.printStackTrace();
		}
		
	}

	/**
	 * Expects msg with:
	 *  -Int: givenBound
	 * 	-ReceiverPortIdentifier
	 * @param msg
	 * 
	 * writes reply with:
	 * 	- Message type
	 *  - int Solutions
	 */
	private synchronized void handleGatherRequest(ReadMessage msg) {
		//we finish all our current jobs (wait for)
		while (jobs.hasJobs()>0)
			try { Thread.sleep(100); } catch (InterruptedException e) {};
			
			
		ReceivePortIdentifier id = null;
		try {
			//Setup message
			int givenBound = msg.readInt();
			id = (ReceivePortIdentifier) msg.readObject();
			otoSend.connect(id);
			WriteMessage reply = otoSend.newMessage();
			reply.writeByte(MessageTypes.gatherReply);
			
			//Count solutions
			int isolut = countOwnSolutions(givenBound);
			//Pack and send
			reply.writeInt(isolut);
			reply.send();
			reply.finish();
		} catch (ClassNotFoundException | IOException e) {
			e.printStackTrace();
		} finally {
			try {
				otoSend.disconnect(id);
			} catch (IOException e) {}
		}
		
		//Shut down system
		systemRunning = false;
	}

	/**
	 * sends a message with jobs (or none)
	 * Message:
	 * 	-message type
	 * 	-number of boards sending
	 * 	-Boards[number of boards]
	 * @param msg
	 * msg expects:
	 * 	-ReceivePortIdentifier
	 */
	private synchronized void handleJobRequest(ReadMessage msg) {
		//Determine jobs
		int hasJobs = jobs.hasJobs();
		
		int nsend = hasJobs - jobRequestTreshold;
		
		if (nsend > jobRequestMaxReply)
			nsend = jobRequestMaxReply;
		else if (nsend < 0)
			nsend = 0;
		
		WriteMessage reply = null;
		ReceivePortIdentifier id = null;
		try {
			id = (ReceivePortIdentifier) msg.readObject();
		} catch (ClassNotFoundException | IOException e1) {
			e1.printStackTrace();
			return;
		}
		
		// Connect and create message
		try {
			otoSend.connect(id);
			reply = otoSend.newMessage();
			reply.writeByte(MessageTypes.jobRequestReply);
		}catch (IOException e) {
			return;
		}
		
		//Fill message with boards
		// Keep track of taken jobs, if something failes, add them back to jobs
		ArrayList<Board> sendJobs = new ArrayList<Board>();
		try {
			reply.writeInt(nsend);
			for (int i = 0; i < nsend; i++) {
				Board b = jobs.get();
				sendJobs.add(b);
				b.fillMessage(reply);
			}
			reply.send();
			reply.finish();
		}catch (IOException e) {
			//REstore jobs!
			for (Board i : sendJobs)
				jobs.add(i);
		}finally {
			try {
				otoSend.disconnect(id);
			} catch (IOException e) {}
		}
	}

	public void handleSignal(String msg) {
		if (msg == null)
			return;

		int i = 0;
		try {
			i = Integer.parseInt(msg);
		} catch (NumberFormatException e) {
			return;
		}

		if (i <= 0)
			return;

		if (jobs.getBoundLimit() > i)
			jobs.setBoundLimit(i);
	}

	private void endIbis() {
		try {
			broadcastReceive.close(1000);
		} catch (IOException e) {
			e.printStackTrace();
		}
		try {
			otmReceive.close(1000);
		} catch (IOException e) {
			e.printStackTrace();
		}
		try {
			otoSend.close();
		} catch (IOException e) {
			e.printStackTrace();
		}
		try {
			myIbis.end();
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	public void exit() {
		endIbis();
	}

}