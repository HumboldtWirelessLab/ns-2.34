#
#  Copyright 2002, Univerity of Colorado at Boulder.                        
#                                                                            
#                         All Rights Reserved                                
#                                                                            
#  Permission to use, copy, modify, and distribute this software and its    
#  documentation for any purpose other than its incorporation into a        
#  commercial product is hereby granted without fee, provided that the      
#  above copyright notice appear in all copies and that both that           
#  copyright notice and this permission notice appear in supporting         
#  documentation, and that the name of the University not be used in        
#  advertising or publicity pertaining to distribution of the software      
#  without specific, written prior permission.                              
#                                                                            
#  UNIVERSITY OF COLORADO DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS      
#  SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND        
#  FITNESS FOR ANY PARTICULAR PURPOSE.  IN NO EVENT SHALL THE UNIVERSITY    
#  OF COLORADO BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL         
#  DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA      
#  OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER       
#  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR         
#  PERFORMANCE OF THIS SOFTWARE.                                            
#
# nsclick-simple-hybrid.tcl
#
# A sample nsclick script simulating a small hybrid wired/wireless
#

#
# Set some general simulation parameters
#

#
# Unity gain, omnidirectional antennas, centered 1.5m above each node.
# These values are lifted from the ns-2 sample files.
#
Antenna/OmniAntenna set X_ 0
Antenna/OmniAntenna set Y_ 0
Antenna/OmniAntenna set Z_ 1.5
Antenna/OmniAntenna set Gt_ 1.0
Antenna/OmniAntenna set Gr_ 1.0

#
# Initialize the SharedMedia interface with parameters to make
# it work like the 914MHz Lucent WaveLAN DSSS radio interface
# These are taken directly from the ns-2 sample files.
#
Phy/WirelessPhy set CPThresh_ 10.0
Phy/WirelessPhy set CSThresh_ 1.559e-11
Phy/WirelessPhy set RXThresh_ 3.652e-10
Phy/WirelessPhy set Rb_ 2*1e6
Phy/WirelessPhy set Pt_ 0.2818
Phy/WirelessPhy set freq_ 914e+6 
Phy/WirelessPhy set L_ 1.0

#
# Disable RTS/CTS handshake for the bridge to work
#
Mac/802_11 set RTSThreshold_ 2500

# 
# Set the size of the playing field and the topography.
#
set xsize 100
set ysize 100
set wtopo	[new Topography]
$wtopo load_flatgrid $xsize $ysize

#
# The network channel, physical layer, MAC, propagation model,
# and antenna model are all standard ns-2.
#  
set wirelesschan Channel/WirelessChannel
set wiredchan Channel

set wirelessphy Phy/WirelessPhy
set wiredphy Phy/WiredPhy

set wirelessmac Mac/802_11
set wiredmac Mac/802_3

set netprop Propagation/TwoRayGround
set antenna Antenna/OmniAntenna

#
# We have to use a special queue and link layer. This is so that
# Click can have control over the network interface packet queue,
# which is vital if we want to play with, e.g. QoS algorithms.
#
set netifq	Queue/ClickQueue
set netll	LL/Ext
LL set delay_			1ms

#
# These are pretty self-explanatory, just the number of nodes
# and when we'll stop.
#
set wirednodecount 3
set wirelessnodecount 3
set bridgenodecount 1
set nodecount   7
set stoptime	10.0

#
# With nsclick, we have to worry about details like which network
# port to use for communication. This sets the default ports to 5000.
#
Agent/Null set sport_		5000
Agent/Null set dport_		5000

Agent/CBR set sport_		5000
Agent/CBR set dport_		5000

#
# Standard ns-2 stuff here - create the simulator object.
#
set ns_		[new Simulator]

#
# Create and activate trace files.
#
set tracefd	[open "nsclick-simple-hybrid.tr" w]
set namtrace    [open "nsclick-simple-hybrid.nam" w]
$ns_ trace-all $tracefd
$ns_ namtrace-all-wireless $namtrace $xsize $ysize
$ns_ use-newtrace


#
# Create the "god" object. This is another artifact of using
# the mobile node type. We have to have this even though
# we never use it.
#
set god_ [create-god $nodecount]

#
# Tell the simulator to create Click nodes.
#
Simulator set node_factory_ Node/MobileNode/ClickNode

#
# Create a network Channel for the nodes to use. One channel
# per LAN. Also set the propagation model to be used.
#
set wired_chan_ [new $wiredchan]
set wireless_chan_ [new $wirelesschan]
set prop_ [new $netprop]

#
# In nsclick we have to worry about assigning IP and MAC addresses
# to out network interfaces. Here we generate a list of IP and MAC
# addresses, one per node since we've only got one network interface
# per node in this case. Also note that this scheme only works for
# fewer than 255 nodes, and we aren't worrying about subnet masks.
#
set iptemplate "192.168.1.%d"
set mactemplate "00:03:47:70:89:%0x"
for {set i 0} {$i < $wirednodecount} {incr i} {
    set wired_node_ip($i) [format $iptemplate [expr $i+1]]
    set wired_node_mac($i) [format $mactemplate [expr $i+1]]
}

set iptemplate "192.168.2.%d"
set mactemplate "00:03:47:70:8A:%0x"
for {set i 0} {$i < $wirelessnodecount} {incr i} {
    set wireless_node_ip($i) [format $iptemplate [expr $i+1]]
    set wireless_node_mac($i) [format $mactemplate [expr $i+1]]
}


#
# We set the routing protocol to "Empty" so that ns-2 doesn't do
# any packet routing. All of the routing will be done by the
# Click script.
#
$ns_ rtproto Empty

#
# Here is where we actually create all of the nodes.
# We'll create the wired, wireless, and the bridge node
# separately.
#

#
# Start with the wireless nodes
#
for {set i 0} {$i < $wirelessnodecount } {incr i} {
    set wireless_node_($i) [$ns_ node]

    #
    # After creating the node, we add one wireless network interface to
    # it. By default, this interface will be named "eth0". If we
    # added a second interface it would be named "eth1", a third
    # "eth2" and so on.
    #
    $wireless_node_($i) add-interface $wireless_chan_ $prop_ $netll \
	    $wirelessmac $netifq 1 $wirelessphy $antenna $wtopo

    #
    # Now configure the interface eth0
    #
    $wireless_node_($i) setip "eth0" $wireless_node_ip($i)
    $wireless_node_($i) setmac "eth0" $wireless_node_mac($i)

    #
    # Set some node properties
    #
    $wireless_node_($i) random-motion 0
    $wireless_node_($i) topography $wtopo
    $wireless_node_($i) nodetrace $tracefd

    #
    # The node name is used by Click to distinguish information
    # coming from different nodes. For example, a "Print" element
    # prepends this to the printed string so it's clear exactly
    # which node is doing the printing.
    #
    [$wireless_node_($i) set classifier_] setnodename "wirelessnode$i-hybrid"
    
    #
    # Load the appropriate Click router script for the node.
    #
    [$wireless_node_($i) entry] loadclick "nsclick-simple-lan.click"
}

#
# Now create the wired nodes
#
for {set i 0} {$i < $wirednodecount } {incr i} {
    set wired_node_($i) [$ns_ node]

    #
    # After creating the node, we add one wired network interface to
    # it. By default, this interface will be named "eth0". If we
    # added a second interface it would be named "eth1", a third
    # "eth2" and so on.
    #
    $wired_node_($i) add-wired-interface $wired_chan_ $netll $wiredmac \
	$netifq 1 $wiredphy

    #
    # Now configure the interface eth0
    #
    $wired_node_($i) setip "eth0" $wired_node_ip($i)
    $wired_node_($i) setmac "eth0" $wired_node_mac($i)

    #
    # Set some node properties
    #
    $wired_node_($i) random-motion 0
    $wired_node_($i) topography $wtopo
    $wired_node_($i) nodetrace $tracefd

    #
    # The node name is used by Click to distinguish information
    # coming from different nodes. For example, a "Print" element
    # prepends this to the printed string so it's clear exactly
    # which node is doing the printing.
    #
    [$wired_node_($i) set classifier_] setnodename "wirednode$i-hybrid"
    
    #
    # Load the appropriate Click router script for the node.
    # All nodes in this simulation are using the same script,
    # but there's no reason why each node couldn't use a different
    # script.
    #
    [$wired_node_($i) entry] loadclick "nsclick-simple-lan.click"
}

#
# Finally make the bridge node
#
set bridge_node_ [$ns_ node]
$bridge_node_ add-wired-interface $wired_chan_ $netll $wiredmac \
	$netifq 1 $wiredphy
$bridge_node_ add-interface $wireless_chan_ $prop_ $netll \
	    $wirelessmac $netifq 1 $wirelessphy $antenna $wtopo
$bridge_node_ setpromiscuous 0 1
$bridge_node_ setpromiscuous 1 1

$bridge_node_ random-motion 0
$bridge_node_ topography $wtopo
$bridge_node_ nodetrace $tracefd

[$bridge_node_ entry] loadclick "nsclick-simple-bridge.click"
[$bridge_node_ set classifier_] setnodename "bridgenode-hybrid"

# 
# Define node network traffic. There isn't a whole lot going on
# in this simple test case, we're just going to have the first wireless node
# send packets to the first wired node, starting at 1 second, and ending at 10.
# There are Perl scripts available to automatically generate network
# traffic.
#


#
# Start transmitting at $startxmittime, $xmitrate packets per second.
#
set startxmittime 1
set xmitrate 4
set xmitinterval 0.25
set packetsize 64

#
# We use the "raw" packet type, which sends real packet data
# down the pipe.
#
set raw_(0) [new Agent/Raw]
$ns_ attach-agent $wireless_node_(0) $raw_(0)

set null_(0) [new Agent/Null]
$ns_ attach-agent $wired_node_(0) $null_(0)

#
# The CBR object is just the default ns-2 CBR object, so
# no change in the meaning of the parameters.
#
set cbr_(0) [new Application/Traffic/CBR]
$cbr_(0) set packetSize_ $packetsize
$cbr_(0) set interval_ $xmitinterval
$cbr_(0) set random_ 0
$cbr_(0) set maxpkts_ [expr ($stoptime - $startxmittime)*$xmitrate]
$cbr_(0) attach-agent $raw_(0)

#
# The Raw agent creates real UDP packets, so it has to know
# the source and destination IP addresses and port numberes.
#
$raw_(0) set-srcip [$wireless_node_(0) getip eth0]
$raw_(0) set-srcport 5000
$raw_(0) set-destport 5000
$raw_(0) set-destip [$wired_node_(0) getip eth0]

$ns_ at $startxmittime "$cbr_(0) start"


$wireless_node_(0) set X_ 10
$wireless_node_(0) set Y_ 50
$wireless_node_(0) set Z_ 0

$wireless_node_(1) set X_ 50
$wireless_node_(1) set Y_ 50
$wireless_node_(1) set Z_ 0

$wireless_node_(2) set X_ 90
$wireless_node_(2) set Y_ 50
$wireless_node_(2) set Z_ 0

$bridge_node_ set X_ 50
$bridge_node_ set Y_ 10
$bridge_node_ set Z_ 0

$wired_node_(0) set X_ 10
$wired_node_(0) set Y_ 0
$wired_node_(0) set Z_ 0

$wired_node_(1) set X_ 50
$wired_node_(1) set Y_ 0
$wired_node_(1) set Z_ 0

$wired_node_(2) set X_ 90
$wired_node_(2) set Y_ 0
$wired_node_(2) set Z_ 0
#
# This sizes the nodes for use in nam.
#
for {set i 0} {$i < $wirelessnodecount} {incr i} {
    $ns_ initial_node_pos $wireless_node_($i) 10
    [$wireless_node_($i) entry] runclick
}

for {set i 0} {$i < $wirednodecount} {incr i} {
    $ns_ initial_node_pos $wired_node_($i) 10
    [$wired_node_($i) entry] runclick
}

$ns_ initial_node_pos $bridge_node_ 10

#
# Stop the simulation
#
$ns_ at  $stoptime.000000001 "puts \"NS EXITING...\" ; $ns_ halt"

#
# Let nam know that the simulation is done.
#
$ns_ at  $stoptime	"$ns_ nam-end-wireless $stoptime"


puts "Starting Simulation..."
$ns_ run




