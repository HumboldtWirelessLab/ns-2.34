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
#

Class ClickNode -superclass Node/MobileNode

Node/MobileNode/ClickNode instproc init args {
    $self instvar nifs_ netif_ mac_ ifq_ ll_ dmux_
    set ns [Simulator instance]
    eval $self next $args
    set nifs_ 0
    $self register-module [new RtModule/Click]

    #
    # This Trace Target is used to log changes in direction
    # and velocity for the mobile node.
    #
    set tracefd [$ns get-ns-traceall]
    if {$tracefd != "" } {
	$self nodetrace $tracefd
	#$self agenttrace $tracefd
    }

    set namtracefd [$ns get-nam-traceall]
    if {$namtracefd != "" } {
	$self namattach $namtracefd
    }

}

Node/MobileNode/ClickNode instproc getip { ifname } {
    [$self entry] getip $ifname
}

Node/MobileNode/ClickNode instproc getmac { ifname } {
    [$self entry] getmac $ifname
}

Node/MobileNode/ClickNode instproc setip { ifname ipaddr } {
    [$self entry] setip $ifname $ipaddr
}

Node/MobileNode/ClickNode instproc setmac { ifname macaddr } {
    [$self entry] setmac $ifname $macaddr
}

Node/MobileNode/ClickNode instproc add-route { dst target } {
    #puts "ClickNode does routing via Click - not ns."
}

Node/MobileNode/ClickNode instproc delete-route args {
    #puts "ClickNode does routing via Click - not ns."
}

Node/MobileNode/ClickNode instproc route-notify { module } {
    #puts "ClickNode does routing via Click - not ns."
}

#
# The following setups up link layer, mac layer, network interface
# and physical layer structures for the click node. Stolen from
# the MobileNode code, and then modified a bit, e.g. the ARP
# stuff got removed.
#
Node/MobileNode/ClickNode instproc add-interface { channel pmodel lltype mactype \
					    qtype qlen iftype anttype topo} {

    $self instvar nifs_ netif_ mac_ ifq_ ll_
    set ns [Simulator instance]
    set t $nifs_
    incr nifs_

    set netif_($t)	[new $iftype]		;# interface
    set mac_($t)	[new $mactype]		;# mac layer
    set ifq_($t)	[new $qtype]		;# interface queue
    set ll_($t)	[new $lltype]		;# link layer
    set ant_($t)    [new $anttype]

    set namfp [$ns get-nam-traceall]

    #
    # Local Variables
    #
    set nullAgent_ [$ns set nullAgent_]
    set netif $netif_($t)
    set mac $mac_($t)
    set ifq $ifq_($t)
    set ll $ll_($t)

    #
    # Link Layer
    #
    $ll mac $mac
    $ll down-target $ifq
    $ll up-target [$self entry]
    # Stuff the link layer into the next available classifier slot, and
    # use that slot number as the network id
    $ll setid [[$self entry] installNext $ll]
    # Set the associated MAC layer address
    $ll set macDA_ [$mac id]

    #
    # Interface Queue
    #
    $ifq target $mac
    $ifq set limit_ $qlen
    set drpT [cmu-trace Drop "IFQ" $self]
    $ifq drop-target $drpT
    if { $namfp != "" } {
	$drpT namattach $namfp
    }
    if {$qtype == "Queue/ClickQueue"} {
	$ifq setclickclassifier [$self entry]
	$ll ifq $ifq
    }

    #
    # Mac Layer
    #
    $mac netif $netif
    $mac up-target $ll
    $mac down-target $netif
    set god_ [God instance]
    if {$mactype == "Mac/802_11"} {
	# XXX this is a hack to handle multiple interfaces per node.
	$mac nodes [expr 4*[$god_ num_nodes]]
    }
    #
    # Network Interface
    #
    $netif channel $channel
    $netif up-target $mac
    $netif propagation $pmodel	;# Propagation Model
    $netif node $self		;# Bind node <---> interface
    $netif antenna $ant_($t)
    #
    # Physical Channel
    #
    $channel addif $netif

        # List-based improvement
	# For nodes talking to multiple channels this should
	# be called multiple times for each channel
	$channel add-node $self		

	# let topo keep handle of channel
	$topo channel $channel	

    
    # ============================================================
    
    if { [Simulator set MacTrace_] == "ON" } {
	#
	# Trace RTS/CTS/ACK Packets
	#
	set rcvT [cmu-trace Recv "MAC" $self]
	$mac log-target $rcvT
	if { $namfp != "" } {
	    $rcvT namattach $namfp
	}
	#
	# Trace Sent Packets
	#
	set sndT [cmu-trace Send "MAC" $self]
	$sndT target [$mac down-target]
	$mac down-target $sndT
	if { $namfp != "" } {
	    $sndT namattach $namfp
	}
	#
	# Trace Received Packets
	#
	set rcvT [cmu-trace Recv "MAC" $self]
	$rcvT target [$mac up-target]
	$mac up-target $rcvT
	if { $namfp != "" } {
	    $rcvT namattach $namfp
	}
	#
	# Trace Dropped Packets
	#
	set drpT [cmu-trace Drop "MAC" $self]
	$mac drop-target $drpT
	if { $namfp != "" } {
	    $drpT namattach $namfp
	}
    } else {
	$mac log-target [$ns set nullAgent_]
	$mac drop-target [$ns set nullAgent_]
    }
    
    # ============================================================
    
    $self addif $netif
}


#
# The following setups up link layer, mac layer, network interface
# and physical layer structures for the click node. Stolen from
# the MobileNode code, and then modified a bit, e.g. the ARP
# stuff got removed.
#
Node/MobileNode/ClickNode instproc add-wired-interface { channel lltype mactype qtype qlen iftype} {

    $self instvar nifs_ netif_ mac_ ifq_ ll_
    set ns [Simulator instance]
    set t $nifs_
    incr nifs_

    set netif_($t)	[new $iftype]		;# interface
    set mac_($t)	[new $mactype]		;# mac layer
    set ifq_($t)	[new $qtype]		;# interface queue
    set ll_($t)	[new $lltype]		;# link layer


    set namfp [$ns get-nam-traceall]

    #
    # Local Variables
    #
    set nullAgent_ [$ns set nullAgent_]
    set netif $netif_($t)
    set mac $mac_($t)
    set ifq $ifq_($t)
    set ll $ll_($t)

    #
    # Link Layer
    #
    $ll mac $mac
    $ll down-target $ifq
     $ll up-target [$self entry]
    # Stuff the link layer into the next available classifier slot, and
    # use that slot number as the network id
    $ll setid [[$self entry] installNext $ll]
    # Set the associated MAC layer address
    $ll set macDA_ [$mac id]

    #
    # Interface Queue
    #
    $ifq target $mac
    $ifq set limit_ $qlen
    set drpT [cmu-trace Drop "IFQ" $self]
    $ifq drop-target $drpT
    if { $namfp != "" } {
	$drpT namattach $namfp
    }
    if {$qtype == "Queue/ClickQueue"} {
	$ifq setclickclassifier [$self entry]
	$ll ifq $ifq
    }

    #
    # Mac Layer
    #
    $mac netif $netif
    $mac up-target $ll
    $mac down-target $netif
    set god_ [God instance]
    if {$mactype == "Mac/802_11"} {
	$mac nodes [$god_ num_nodes]
    }
    #
    # Network Interface
    #
    $netif channel $channel
    $netif up-target $mac
    $netif node $self		;# Bind node <---> interface
    #
    # Physical Channel
    #
    $channel addif $netif
    
    # ============================================================
    
    if { [Simulator set MacTrace_] == "ON" } {
	#
	# Trace RTS/CTS/ACK Packets
	#
	set rcvT [cmu-trace Recv "MAC" $self]
	$mac log-target $rcvT
	if { $namfp != "" } {
	    $rcvT namattach $namfp
	}
	#
	# Trace Sent Packets
	#
	set sndT [cmu-trace Send "MAC" $self]
	$sndT target [$mac down-target]
	$mac down-target $sndT
	if { $namfp != "" } {
	    $sndT namattach $namfp
	}
	#
	# Trace Received Packets
	#
	set rcvT [cmu-trace Recv "MAC" $self]
	$rcvT target [$mac up-target]
	$mac up-target $rcvT
	if { $namfp != "" } {
	    $rcvT namattach $namfp
	}
	#
	# Trace Dropped Packets
	#
	set drpT [cmu-trace Drop "MAC" $self]
	$mac drop-target $drpT
	if { $namfp != "" } {
	    $drpT namattach $namfp
	}
    } else {
	$mac log-target [$ns set nullAgent_]
	$mac drop-target [$ns set nullAgent_]
    }
    
    # ============================================================
    
    $self addif $netif
}

Node/MobileNode/ClickNode instproc setpromiscuous { whichif ispromisc } {
    $self instvar ll_
    set thell $ll_($whichif)
    $thell setpromiscuous $ispromisc
}

Node/MobileNode/ClickNode instproc dump-namconfig {} {
# Do nothing
}