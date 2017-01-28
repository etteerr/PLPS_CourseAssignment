package sokoban.ipl;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

import ibis.ipl.Ibis;
import ibis.ipl.IbisCapabilities;
import ibis.ipl.IbisFactory;
import ibis.ipl.IbisIdentifier;
import ibis.ipl.MessageUpcall;
import ibis.ipl.PortType;
import ibis.ipl.ReadMessage;
import ibis.ipl.ReceivePort;
import ibis.ipl.ReceiveTimedOutException;
import ibis.ipl.RegistryEventHandler;
import ibis.ipl.SendPort;
import ibis.ipl.WriteMessage;
import ibis.ipl.impl.SendPortIdentifier;

public class IkoPanLayer implements MessageUpcall, RegistryEventHandler {
    
    
    private static final int stepsPerNode = 20;

	PortType portTypeS = new PortType(
    		PortType.COMMUNICATION_RELIABLE,
            PortType.SERIALIZATION_DATA, 
            PortType.RECEIVE_AUTO_UPCALLS,
            PortType.CONNECTION_MANY_TO_ONE,
            PortType.COMMUNICATION_FIFO
            );
    
    PortType portTypeR = new PortType(
    		PortType.COMMUNICATION_RELIABLE,
            PortType.SERIALIZATION_DATA, 
            PortType.RECEIVE_EXPLICIT,
            PortType.RECEIVE_TIMEOUT,
            PortType.CONNECTION_ONE_TO_MANY
            );

    IbisCapabilities ibisCapabilities = new IbisCapabilities(
            IbisCapabilities.ELECTIONS_STRICT,
            IbisCapabilities.TERMINATION,
            IbisCapabilities.MEMBERSHIP_TOTALLY_ORDERED,
            IbisCapabilities.SIGNALS
            );
    
    //Port names
    private static final String serverName = "server";
    private static final String serverPort = "serverPort";
    private static final String replyPort = "replyer";
    
    //Requests
    private static final String boardRequestMessage = "BoardRequest";
    private static final String boardRequestAnswer = "boardAnswer";
	private static final String doneRequestAnswer = "done";
	private static final String replyMessageFailedToSolve = "failed";
	private static final String replyMessageSolutionFound = "Solution";
	private static final String waitRequestAswer = "not ready";
    
    //Global ibis (local)
    Ibis myIbis;
    Board initialBoard;
    boolean appRunning = true;
    
    //is server
    boolean iAmServer = false;
    int shortest = Integer.MAX_VALUE;
    private Object lockShortest = new Object();
    OneToManyHandler otmh;
    ReceivePort serverRecvPort;
    IbisIdentifier server;
    BoardCache boardCache;
    ArrayList<Board> jobs;
    ArrayList<Board> jobsBusyOrDone;
    private Object jobsLock = new Object();
    private ArrayList<IbisIdentifier> boardRequests;
    int bound = 0;
    
    //client data
    int steps = 1;
    SendPort clientSendPort;
    ReceivePort clientRecvReplyPort;
    ArrayList<Board> unsolvedBoards;

	private ArrayList<Board> serverSolutions;

	

    
	/**
	 * Constructor
	 * 	Handles the election of a server.
	 * 	Handles creation of a ibis
	 * 	Handles registering
	 */
	IkoPanLayer() throws Exception {
		//Create cache
		boardCache = new BoardCache();
		
		//Create ibis
		myIbis = IbisFactory.createIbis(ibisCapabilities, this, portTypeS, portTypeR);
		
		//Register for events
		myIbis.registry().enableEvents();
		
		//Elect a server (so, new clients can be added dynamically)
		myIbis.registry().elect(serverName);
		
		//Wait for election (happens through events)
		while(server==null)
			Thread.sleep(100);
		
		//Initialize client if we are not a server
		//if (!server.equals(myIbis.identifier())) 
			initClient();
	}
	
	void shutdown() {
		if (iAmServer) {
			try {
				jobs = new ArrayList<Board>();//Make jobs empty
				Thread.sleep(5000);
				runServerStep(); //handle final requests
				serverRecvPort.disableConnections();
				serverRecvPort.disableMessageUpcalls();
				serverRecvPort.close(1000);
			} catch (IOException | InterruptedException e) {
	
			}
			
			otmh.disconnectAll();
			
			try {
				myIbis.registry().terminate();
			} catch (IOException e) {
	
			}
		}
		
		//General clean
		myIbis.registry().disableEvents();
		
		try {
			if (clientSendPort!=null)
				clientSendPort.close();
		} catch (IOException e) {
	
		}
		
		try {
			if(clientRecvReplyPort!=null)
				clientRecvReplyPort.close(1000);
		} catch (IOException e1) {
	
		}
		
		//Close ibis
		try {
			myIbis.end();
		} catch (IOException e) {
	
		}
	}
	
	public ArrayList<Board> getSolutions() {
		return serverSolutions;
	}

	private void initClient() throws IOException {		
		//If we become the server, make sure everyone exits
		jobs = new ArrayList<Board>();
		
		//Open a port for replies
		clientRecvReplyPort = myIbis.createReceivePort(portTypeR, replyPort);
		clientRecvReplyPort.enableConnections();
		
		//Init array
		unsolvedBoards = new ArrayList<Board>();
	}

	private void initServer() throws Exception {
		if (iAmServer)
			return;
		//myIbis.registry().disableEvents(); //<-- causes hang
		
		iAmServer = true;
		server = myIbis.identifier();
		boardRequests = new ArrayList<IbisIdentifier>();
		serverRecvPort = myIbis.createReceivePort(portTypeS, serverPort, this);
		serverRecvPort.enableConnections();
		serverRecvPort.enableMessageUpcalls();
		SendPort serverReplyPort = myIbis.createSendPort(portTypeR);
		otmh = new OneToManyHandler(serverReplyPort, replyPort);
		serverSolutions = new ArrayList<Board>();
		
		//Enable registry (to allow for newly joined ibises)
		//myIbis.registry().enableEvents(); //<-- causes events to hang
	}

	public void initJobs(Board initBoard) throws InterruptedException {
		//System.out.println("initJobs: "+iAmServer);
		if (!iAmServer) {
			return;
		}
		
		initialBoard = new Board(initBoard);
	
		//Generate jobs
		synchronized(jobsLock){
			jobs = new ArrayList<Board>();
			jobsBusyOrDone = new ArrayList<Board>();
			
			ArrayList<Board> boards = (ArrayList<Board>) initBoard.generateChildren(boardCache);
			for(Board b : boards) {
				jobs.addAll(b.generateChildren(boardCache));
			}
		}
		
		//Initial board output
		System.out.println("Running Sokoban, initial board:");
		System.out.println(initBoard.toString());
	}

	public void run(){
		appRunning = true;
		new Thread(new Runnable() {
		    public void run() {
		    	while(appRunning && !myIbis.registry().hasTerminated() ){
		    		runClientStep();
		    	}
		    }
		}).start();
		while(appRunning && !myIbis.registry().hasTerminated() ) {
			if (iAmServer) 
				runServerStep();
			else
				try {
					Thread.sleep(1000);
				} catch (InterruptedException e) {
				}
				
		}
	}
	
	private void runClientStep(){
		//CHeck connection
		//System.out.println(clientSendPort.lostConnections());
		
		//Request board
		try {
			requestBoard();
		} catch (Exception e) {
			System.err.println("Error while requesting board: "+e.getMessage());
			e.printStackTrace();
			return;
		}
		
		//Get answer
		Board b = getRequestedBoardReply();
		
		
		//Process board
		ArrayList<Board> sols = processBoard(b);
		
		//Send the result
		//If b is solved, send b. If not, send global unsolved
		sendBoardReply(sols);
	}
	
	private void sendBoardReply(ArrayList<Board> solutions) {
		
		if (solutions == null)
			return;
		
		if (unsolvedBoards.isEmpty())
			return;
		
		//Report
		try {
			WriteMessage boardFinished = clientSendPort.newMessage();
			
			if (!solutions.isEmpty()) {
				boardFinished.writeString(replyMessageSolutionFound);
				boardFinished.writeInt(solutions.size());
				for(Board b : solutions)
					b.fillMessage(boardFinished);
				boardFinished.writeInt(unsolvedBoards.size());
				for (Board b : unsolvedBoards) {
					b.fillMessage(boardFinished);
				}
			}else{
				boardFinished.writeString(replyMessageFailedToSolve);
				boardFinished.writeInt(unsolvedBoards.size());
				for (Board b : unsolvedBoards) {
					b.fillMessage(boardFinished);
				}
				//ourBoard.fillMessage(boardFinished);
			}
			//Send message
			boardFinished.send();
			boardFinished.flush();
			boardFinished.finish();
			
		}catch (IOException e) {
			System.err.println("Error while reporting boards to server");
			System.err.println(e.getMessage());
		}
		
	}

	private void runServerStep() {
		//Check recv connection
		serverRecvPort.toString();
		
		synchronized(boardRequests) {
			for(IbisIdentifier id : boardRequests) {
				handleBoardRequest(id);
			}
			boardRequests.clear();
		}
		
	}

	private ArrayList<Board> processBoard(Board recvBoard) {
		if (recvBoard==null)
			//Nothing to to
			return null;
		
		//Init board array
		unsolvedBoards.clear();
		unsolvedBoards.add(recvBoard);
		
//		System.out.println("Solving board...");
		
		//Init temp board array
		HashSet<Board> tmp = new HashSet<Board>();
		ArrayList<Board> solutions = new ArrayList<Board>();
		
		
		if (recvBoard.isSolved()){
			solutions.add(recvBoard);
			return solutions;
		}
		//From bound to bound
		int from = recvBoard.getMoves();
		int to = recvBoard.getMoves()+steps;
		
		//For determined steps, keep solving all boards
		for (int i = from; i < to; i++) {
			for (Board b : unsolvedBoards) {
				//SOlve board
				
				//if solved, store
				if (b.isSolved()) {
					solutions.add(b);
				}else
				
				//And prune
				if (b.getMoves() <= shortest)
					//Step 1
					if (b.getMoves() > i)
						tmp.add(b); //On the bound or out of bound
					else
						tmp.addAll(b.generateChildren(boardCache));
				
				
			}
			
			//add/clear boards and repeat
			//Except for when one is solved
			if (!solutions.isEmpty())
				break;
			
			unsolvedBoards.clear();
			unsolvedBoards.addAll(tmp);
			tmp.clear();
		}
		
		return solutions;
		
	}

	private Board getRequestedBoardReply() {
		ReadMessage reply;
		String cmd = "err";
		
		//Get reply message
		try{
			// set timeout
			reply = clientRecvReplyPort.receive(5000);
			cmd = reply.readString();
		}catch (ReceiveTimedOutException e) {
			reply = null;
			System.err.println("Request timeout");
			return null;
		} catch (IOException e) {
			System.err.println(e.getMessage());
			return null;
		}
		
		//Check if we are done
		if (cmd.equals(doneRequestAnswer)) {
			appRunning = false;
			try {
				reply.finish();
				clientSendPort.disconnect(server, serverPort);
				clientSendPort.close();
			} catch (IOException e) {
				e.printStackTrace();
			}
			return null;
		}
		
		//Check if its our board
		if (cmd.equals(boardRequestAnswer)) {
			Board b;
			try {
				//Set steps
				steps = reply.readInt();
				//Recv boad
				b = new Board(reply);
				//Finish reply
				reply.finish();
				//return our board
				return b;
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} 
		}
		
		if (reply!=null)
			try {
				reply.finish();
				Thread.sleep(500);
			} catch (IOException | InterruptedException e) {
				System.err.println("Error with reply:");
				System.err.println(e.getMessage());
			}
		
		return null;
		
	}

	private void requestBoard() throws IOException {		
		WriteMessage boardRequest;
		boardRequest = clientSendPort.newMessage();
		boardRequest.writeString(boardRequestMessage);
		boardRequest.send();
		try{
			boardRequest.finish();
		}catch (IOException e) {
			boardRequest.reset();
			boardRequest.finish();
			throw e;
		}
	}

	public void waitForEnd() throws IOException {
		myIbis.registry().waitUntilTerminated();
		shutdown();
	}
	
	
	private Board selectBoard(int maxMoves) {
		if (jobs == null)
			return null;
		
		if (jobs.isEmpty())
			return null;
		
		Board selected = null;
		
		ArrayList<Board> toBePruned = new ArrayList<Board>();
		
		synchronized(jobsLock){
			outerloop:
			while(true){
				for (Board b : jobs) {
					if (b.getMoves() < bound) {
						selected = b;
						break outerloop;
					}
					//Prune
					if (b.getMoves() > shortest)
						toBePruned.add(b);
				}
			if (bound < shortest){	
				bound++;
				System.out.print(" "+bound);
			}else
				return null;
			}
			//prune
			jobs.removeAll(toBePruned);
			jobsBusyOrDone.add(selected); //Keep a record of send jobs. If jobs is empty without a solution, network has failed somewhere
			jobs.remove(selected);
			
		} //Release lock
		
		return selected;
	}
	
	private void pruneJobs(int moves) {
		ArrayList<Board> toBePruned = new ArrayList<Board>();
		
		synchronized(jobsLock){
			for (Board b : jobs) {
				//Prune
				if (b.getMoves() > moves)
					toBePruned.add(b);
			}
			
			//prune
//			System.out.println("Pruned " + toBePruned.size() + " jobs.");
			jobs.removeAll(toBePruned);
			
		} //Release lock
	}

	@Override
	public void upcall(ReadMessage msg) throws IOException, ClassNotFoundException {
		//Server handles requests
//		System.out.println("From "+msg.origin().name());
		String req = msg.readString();
//		System.out.println("Request: "+req);
//		System.err.println("Message size : " + msg.remaining());
		
		if (req.equals(boardRequestMessage))
		{
			planBoardRequestHandle(msg);
			msg.finish();
			
		}else if (req.equals(replyMessageSolutionFound)) {
			//Solution found
			//Prune all boards
			//appRunning = false;
			int sols = msg.readInt();
			for(int i = 0; i < sols; i++){
				Board b = new Board(msg);
				if (b.isSolved())
					if (b.moves < shortest)
						shortest = b.moves;
				serverSolutions.add(b);
			}
			addJobs(msg);
			msg.finish(); //Locks shortests in upCall
			pruneJobs(shortest);
			
			//Send limiter
			for (IbisIdentifier i : otmh.getReceivePortIdentifiers()) {
				myIbis.registry().signal(""+shortest, i);
			}
			//remove print statements
//			System.out.println("Solution!");
			//System.out.println(b.toString());
			
		}else if (req.equals(replyMessageFailedToSolve)) {
			addJobs(msg);
			msg.finish();
		}
		
		//TODO remove print statements
//		if (jobs!=null)
//			System.out.println("Jobs: " + jobs.size());
//		else
//			System.out.println("no jobs");
	}
	
	private void planBoardRequestHandle(ReadMessage msg) {
		IbisIdentifier id = msg.origin().ibisIdentifier();
		synchronized(boardRequests) {
			boardRequests.add(id);
		}
	}

	private void handleBoardRequest(IbisIdentifier id) {
		try {
			Board b;
			WriteMessage reply = otmh.getNewMessage(id);
			
			//If jobs is not initialized, let the slaves wait
			if (jobs == null) 
				reply.writeString(waitRequestAswer);
			//If jobs is empty, we are done
			else if ((b=selectBoard(bound))==null) 
				if (!serverSolutions.isEmpty())
					reply.writeString(doneRequestAnswer);
				else
					reply.writeString(waitRequestAswer);
			//write a formal letter
			else {
				reply.writeString(boardRequestAnswer);
				reply.writeInt(stepsPerNode);
				b.fillMessage(reply);
			}
			//Send letter
			reply.send();
			reply.flush();
			reply.finish();

		}catch (Exception e) {
			//e.printStackTrace();
			System.err.println("Error while handling request: "+e.getMessage());
			e.printStackTrace();
		}
		
	}

	private void addJobs(ReadMessage msg) throws IOException {
		int im = msg.readInt();
		synchronized(jobsLock){
			for (int i = 0; i < im; i++) {
				Board b = new Board(msg);
				//Prune
				if (b.getMoves() <= shortest)
					jobs.add(b);
			}
		}
	}

	
	@Override
	public void died(IbisIdentifier dogTag) {
		if (dogTag.equals(server)) {
			//Reelect a server
			try {
				myIbis.registry().elect(serverName);
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}
		
		otmh.disconnect(dogTag);
	}

	@Override
	public void electionResult(String electionName, IbisIdentifier trumpPerson) {
		//If election is not for sever.... than we ignore election
		if (!electionName.equals( serverName))
			return;
		
		//Initialize correct ports if we are server
		if (trumpPerson.equals(myIbis.identifier())) {
			try {
//				System.err.println(myIbis.identifier().name()+" Got elected Server");
				initServer();
			} catch (Exception e) {
				e.printStackTrace();
				return;
			}
		}
		
		//Connect client to sever
		try {
//			if (!trumpPerson.equals(myIbis.identifier())) 
				connectToServer(trumpPerson);
			server = trumpPerson;
		}catch (IOException e) {
			//Could not connect
			//Make dead assumption
			System.err.println("Assuming dead server");
			try {
				myIbis.registry().assumeDead(trumpPerson);
				Thread.sleep(100);
				myIbis.registry().elect(serverName);
				return;
			} catch (IOException e1) {
				// TODO Auto-generated catch block
				System.err.println(e1.getMessage());
			} catch (InterruptedException e1) {
				System.err.println("sleep error");
			}
		}
		
		
	}

	private void connectToServer(IbisIdentifier target) throws IOException {
		try {
			if (clientSendPort == null) {
				clientSendPort = myIbis.createSendPort(portTypeS);
				
			}else
				clientSendPort.disconnect(server, serverName);;
		} catch (IOException e1) {
			//Nothing to do, not a biggy
			//clientSendPort = null;
		}
		
		clientSendPort.connect(target, serverPort);

	}
	

	@Override
	public void gotSignal(String signal, IbisIdentifier source) {
		if (signal==null)
			return;
		
		if (Integer.parseInt(signal)==0)
			return;
		
		if (!source.equals(server))
			return;
		
		if(lockShortest==null)
			return;
		
		synchronized(lockShortest) {
			shortest = Integer.parseInt(signal);
		}
	}

	@Override
	public void joined(IbisIdentifier who) {
		// TODO Auto-generated method stub
//		System.err.println("Pool joined: " + who.name());
		if ((!who.equals(myIbis.identifier())) && iAmServer){

		}
	}

	@Override
	public void left(IbisIdentifier who) {
		if (who.equals(server)) {
			//Server shutdown (not crash)
			
		}else if (iAmServer) {
			otmh.disconnect(who);
		}
		
	}

	@Override
	public void poolClosed() {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void poolTerminated(IbisIdentifier arg0) {
		// TODO Auto-generated method stub
		
	}
}
