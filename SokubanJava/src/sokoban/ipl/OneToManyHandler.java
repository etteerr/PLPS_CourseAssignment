package sokoban.ipl;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

import ibis.ipl.ConnectionFailedException;
import ibis.ipl.IbisIdentifier;
import ibis.ipl.ReceivePortIdentifier;
import ibis.ipl.SendPort;
import ibis.ipl.WriteMessage;

public class OneToManyHandler {
	
	private SendPort sendPort;
	private String portName;
	private HashMap<IbisIdentifier, ReceivePortIdentifier> idToPortMap;	
	
	public OneToManyHandler(SendPort port, String portname) {
		sendPort = port;
		portName = portname;
		idToPortMap = new HashMap<IbisIdentifier, ReceivePortIdentifier>();
	}


	public WriteMessage getNewMessage(IbisIdentifier ibisIdentifier) {
		ReceivePortIdentifier id = idToPortMap.get(ibisIdentifier);
		
		if (id==null) {
			id = connectTo(ibisIdentifier);
		
			if (id == null)
				return null;
			
			//Add id
			idToPortMap.put(ibisIdentifier, id);
		}
			
		try {
			return sendPort.newMessage();
		}catch (IOException e) {
			try {
				sendPort.disconnect(id);
			} catch (IOException e1) {
				System.err.println("Could not create message for " + ibisIdentifier.name());
			}
			
			idToPortMap.remove(ibisIdentifier);
			return null;
		}
	}

	private ReceivePortIdentifier connectTo(IbisIdentifier ibisIdentifier) {
		try {
			ReceivePortIdentifier id = sendPort.connect(ibisIdentifier, portName);
			return id;
		} catch (ConnectionFailedException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		return null;
	}
	
	public void close() {
		disconnectAll();
		try {
			sendPort.close();
		} catch (IOException e) {
			//Failed to close
		}
	}

	public void disconnectAll() {
		for (Map.Entry<IbisIdentifier, ReceivePortIdentifier> i : idToPortMap.entrySet()) {
			disconnect(i.getValue());
		}
	}

	public void disconnect(IbisIdentifier who) {
		ReceivePortIdentifier id = idToPortMap.get(who);
		
		if (id!=null)
			disconnect(id);
	}
	
	public void disconnect(ReceivePortIdentifier id) {
		try {
			sendPort.disconnect(id);
		} catch (IOException e) {
			//Failed to dc
		}
	}


	public Set<IbisIdentifier> getReceivePortIdentifiers() {
		return idToPortMap.keySet();
		
	}
	
}
