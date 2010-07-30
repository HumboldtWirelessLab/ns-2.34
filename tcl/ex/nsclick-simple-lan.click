//
//  Copyright 2002, Univerity of Colorado at Boulder.                        
//                                                                            
//                         All Rights Reserved                                
//                                                                            
//  Permission to use, copy, modify, and distribute this software and its    
//  documentation for any purpose other than its incorporation into a        
//  commercial product is hereby granted without fee, provided that the      
//  above copyright notice appear in all copies and that both that           
//  copyright notice and this permission notice appear in supporting         
//  documentation, and that the name of the University not be used in        
//  advertising or publicity pertaining to distribution of the software      
//  without specific, written prior permission.                              
//                                                                            
//  UNIVERSITY OF COLORADO DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS      
//  SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND        
//  FITNESS FOR ANY PARTICULAR PURPOSE.  IN NO EVENT SHALL THE UNIVERSITY    
//  OF COLORADO BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL         
//  DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA      
//  OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER       
//  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR         
//  PERFORMANCE OF THIS SOFTWARE.                                            
//

// nsclick-simple-lan.click
//
// This is a simple and stupid flat routing mechanism.
// It broadcasts ARP requests if it wants to find a destination
// address, and it responds to ARP requests made for it.

elementclass DumbRouter {
  $myaddr, $myaddr_ethernet |

  class :: Classifier(12/0806 20/0001,12/0806 20/0002, -);
  mypackets :: IPClassifier(dst host $myaddr,-);
  myarpquerier :: ARPQuerier($myaddr,$myaddr_ethernet);
  myarpresponder :: ARPResponder($myaddr $myaddr_ethernet);
  ethout :: Queue -> ToSimDevice(eth0);

  FromSimDevice(eth0,4096)
	-> Print(eth0,64)
	-> ToDump(in_eth0,PER_NODE 1)
	-> HostEtherFilter($myaddr_ethernet)
	-> class;
	
  // ARP queries from other nodes go to the ARP responder module
  class[0] -> myarpresponder;

  // ARP responses go to our query module
  class[1] -> [1]myarpquerier;

  // All other packets get checked to see if they're meant for us
  class[2]				
	-> Strip(14)
	-> CheckIPHeader
	-> MarkIPHeader
	-> GetIPAddress(16)
	-> mypackets; 

  // Packets for us go to "tap0" which sends them to the kernel
  mypackets[0]
	-> IPPrint(tokernel) 
	-> ToDump(tokernel,2000,IP,PER_NODE 1) 
	-> ToSimDevice(tap0,IP);

  // Packets for other folks or broadcast packets get discarded
  mypackets[1]
	-> Print(discard,64)
	-> ToDump(discard,2000,PER_NODE 1)
	-> Discard;

  // Packets sent out by the "kernel" get pushed into the ARP query module
  FromSimDevice(tap0,4096)
	-> CheckIPHeader 
	-> IPPrint(fromkernel) 
	-> ToDump(fromkernel,2000,IP,PER_NODE 1)
	-> GetIPAddress(16)
	-> myarpquerier;

  // Both the ARP query and response modules send data out to
  // the simulated network device, eth0.
  myarpquerier
	-> Print(fromarpquery,64)
	-> ToDump(out_arpquery,PER_NODE 1)
	-> ethout;

  myarpresponder
	-> Print(arpresponse,64)
	-> ToDump(out_arprespond,PER_NODE 1)
	-> ethout;
}


u :: DumbRouter(eth0,eth0);



