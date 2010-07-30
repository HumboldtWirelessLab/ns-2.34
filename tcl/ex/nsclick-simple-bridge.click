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

// nsclick-simple-bridge.click
//
// This is a simple and stupid network "bridge." Packets coming
// in off of eth0 are pumped out on eth1, and packets coming
// in off of eth1 are pumped out on eth0.
//

FromSimDevice(eth0,4096)
	-> Queue
	-> ToSimDevice(eth1);
	
FromSimDevice(eth1,4096)
	-> Queue
	-> ToSimDevice(eth0);


