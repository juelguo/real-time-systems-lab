# Dependable Real-Time Systems HWA1 Report

## Basic Information

Course: EDA423 / DIT173 Dependable Real-Time Systems
Assignment: Homework Assignment 1
Group: DRTS Group 3
Meta-group: Group 1
Date: 2026-04-28

Group members:

- Jueliang Guo
- Wenqi Zhao

## Overall Protocol

### Protocol Design

As required by the assignment, our group needs to work with two other groups to implement the round-robin melody player and related functions, including conductorship claiming, conductorship switching, and failure detection. In order to reach this goal, the meta-group agreed on a common CAN message protocol. All groups use the same message IDs and payload meanings, so boards can communicate with each other, including discovery, control, heartbeat, and failure messages.

The protocol uses the `msgId` field of the `CANMsg` structure to identify the message type. The `nodeId` field identifies the sender, which is fixed for each board. Additional data is placed in the `buff` array when needed.

### Message Types

This table lists all message IDs that we used in our implementation.

| Message | `msgId` | Payload | Purpose |
| --- | ---: | --- | --- |
| `CAN_MSG_CONDUCTOR_COMMAND` | 1 | `buff[0] = sub-cmd`, `buff[1+] = value` | Broadcasts conductor commands such as start, stop, key, and tempo. |
| `CAN_MSG_DISCOVERY_PING` | 2 | No payload | Used to discover which boards are currently connected and responsive. |
| `CAN_MSG_DISCOVERY_REPLY` | 3 | `buff[0] = replying node ID` | Sent as a response to a discovery ping. The payload identifies the node that received and answered the ping. |
| `CAN_MSG_CONDUCTOR_ANNOUNCE` | 4 | No required payload; sender `nodeId` identifies the conductor | Announces which board is currently conductor. |
| `CAN_MSG_TOKEN` | 5 | `buff[0] = noteIndex` | Transfers responsibility for the current note in the round-robin melody. |
| `CAN_MSG_HEARTBEAT` | 6 | No required payload; sender `nodeId` identifies the active note player | Shows that the board currently playing a note is still alive. |
| `CAN_MSG_ANNOUNCE_NODE_FAILURE` | 7 | `buff[0] = failedNode` | Announces that a node has been detected as failed. |

### Conductor Command Subcommands

Subcommands are used by a conductor to control other musician boards. The message `CAN_MSG_CONDUCTOR_COMMAND` uses `buff[0]` as a subcommand field.

| Subcommand | Value in `buff[0]` | Extra Payload | Purpose |
| --- | ---: | --- | --- |
| `CAN_SUB_START` | 1 | None | Starts melody playback. |
| `CAN_SUB_STOP` | 2 | None | Stops melody playback. |
| `CAN_SUB_SET_KEY` | 3 | `buff[1] = key` | Updates the key offset used by the melody. |
| `CAN_SUB_SET_TEMPO` | 4 | `buff[1] = (tempo >> 8) & 0xFF`, `buff[2] = tempo & 0xFF` | Updates the tempo. Two bytes are used because tempo can be larger than 255. |

The initial state has no default conductor. All boards start as musicians. Before the melody can be started, one board must claim conductorship.

## Problem 1: Listen to the Round Robin

### Design Overview

We need to extend the local melody player into a collaborative melody player. Every board in this network will be responsible for a note of the whole melody. This means all boards share a single logical melody position. We use the message `CAN_MSG_TOKEN` to tell other boards what we have played. Because the CAN message is broadcast on the network, all other boards can receive the message. We use this mechanism to achieve the synchronization between all boards.

The main rule is: `responsible_rank = note_index % active_count`

Here, `active_count` is the number of boards currently available for playback. Each board calculates its own rank from the sorted list of active node IDs. If the board's rank equals `responsible_rank`, that board plays the note. Otherwise, it waits for a later token. This ensures that only one board plays at a time.

The initial melody state is stopped. The default melody parameters are: `key = 0`, `tempo = 120 bpm`

### Board Identity and Rank

Each board is configured with a unique node ID using: `node <id>`

Valid node IDs are in the range `1..14`. Node ID `0` is not used as a normal node ID. The node ID is encoded in the lower part of the CAN identifier and is also used in the application-level protocol.

The rank is dynamically calculated, our board keeps a sorted `active_nodes` list. The rank of a board is its index in this sorted list. Every time the active_nodes list is updated, the rank should be recalculated.

### Network Discovery

**Before playback starts, discovery is used to decide how many boards are in the network.**

When our board starts up, we need to set our board node ID; usually we set `node 3`, as we are group 3. Once the node ID is set, our board will send out a ping message, which we call the discovery process. As the overall protocol described, other boards that receive a ping message should reply to it. Our board will construct an `active node list` based on the reply message.
Also when our board receives a discovery ping, we record the sender as known and active, then reply with our own node ID. After the 500 ms discovery window, every board can derive the number of boards in the network from the size of `active_nodes`.

In our implementation, membership information is stored in:

- `active_nodes`: nodes currently participating in playback.
- `active_count`: number of currently active nodes.
- `known_nodes`: nodes that have been seen before.

### Conductor and Musician Roles

The implementation starts each board in musician mode, with no conductor selected. A board becomes conductor by entering: `claim`

When a board claims conductorship, it broadcasts `CONDUCTOR_ANNOUNCE`. Other boards receive this announcement and configure themselves as musicians if the announced conductor is not themselves. This avoids direct manual forcing of multiple conductors. Commands such as direct `conductor` or `musician` switching are disabled in the final version (those commands are used for the previous course), so conductorship is controlled through the shared announcement protocol.

The conductor is responsible for:

- Starting or stopping the melody.
- Sending key and tempo changes.
- Resetting key and tempo to default values.
- Periodically printing the current tempo when enabled.

Musician boards are responsible for:

- Listening to conductor commands.
- Playing notes when the token reaches their rank.
- Supporting local mute/unmute of audio output.
- Printing `MUTED` periodically when muted and periodic printing is enabled.

### Round-Robin Playback

The `TOKEN` message contains the current note index in the melody. The melody contains 32 notes, so the token index is wrapped into the range `0..31`.

Playback is controlled by token passing. The board that plays a note also schedules the end of that note. When the note finishes, it sends a new token with the next note index.

Our playback sequence is:

1. The conductor receives `play`.
2. The conductor starts discovery.
3. After the discovery timeout (500ms), the conductor sends `TOKEN(0)`.
4. All boards receive the token and calculate `note_index % active_count`.
5. The board whose rank matches the calculated rank plays the note.
6. When the note finishes, that board sends the next token.
7. The process repeats for the whole melody.

Because the next note is started only after the previous note holder sends the next token, the design avoids overlapping note playback. It also avoids drift from independent local timers on different boards.

### Conductor Commands

In our implementation, these commands are case sensitive.

The conductor can start playback using: `p`

The conductor can stop playback using: `q`

The conductor can change tempo using: `t/tempo`

and can change key using: `k/key`

Tempo values are clamped to `60..240 bpm`. Key values are clamped to `-5..5`.

The reset command: `R`, it sets the key back to `0` and the tempo back to `120 bpm`. If the board is currently conductor, the reset values are broadcast to the other boards as conductor commands.

Periodic printing is toggled with: `P`: When periodic printing is enabled on the conductor, it prints the current tempo every 10 seconds.

### Musician Behavior

Musician boards receive the token and conductor commands over CAN. They do not directly control the global melody state, except by participating in later problems such as claiming conductorship.

The local audio mute state is toggled using: `T`. When periodic printing is enabled using `P`, a muted musician prints: `MUTED` every 10 seconds.

### What Worked Well

The token-passing design worked well because it gave the whole meta-group one shared melody position. Since only the board holding the current note sends the next token, the boards do not need to rely on perfectly synchronized clocks.

### Challenges

The main challenge was agreeing on a protocol that all boards could interpret consistently. The message IDs, payload formats, and node ID/rank interpretation had to be fixed before the boards could interoperate.

### What We Learned

We learned that a shared protocol must be defined very clearly before different boards can cooperate. We also learned that using a token makes the melody order easier to control because every board can derive the same current note position from the same message.

### Contribution

Contribution from group member 1 (Jueliang Guo, 100%):
Contribution from group member 2 (Wenqi Zhao, 100%):

## Problem 2: Embrace Your New Conductor

### Design Overview

In this problem, a musician can claim conductorship while the melody continues playing. The new conductor will tell other boards in this network that it becomes a new conductor, and the old conductor should give up its conductorship and become a musician.

Our protocol uses the same `CONDUCTOR_ANNOUNCE` message as in Problem 1. Conductorship is represented by `conductor_id`, and each board stores its local role as either conductor or musician.

### Claim Mechanism

A musician claims conductorship by entering: `claim`. The claiming board immediately switches its local role to conductor and broadcasts: `CONDUCTOR_ANNOUNCE`.

The payload and CAN node ID identify the new conductor. Other boards receive the announcement and update their `conductor_id`.

If the announced conductor ID is equal to the local node ID, the board becomes conductor. Otherwise, it becomes or remains musician. This design is used for the old conductor gives up its conductorship.

### Conflict Resolution

For conflict cases at the CAN bus level, we rely on the arbitration mechanism provided by the CAN protocol itself. The application layer does not implement an additional conflict-resolution mechanism for simultaneous claims.

From the board's point of view, a later received `claim` overwrites the previous conductor. When the current conductor receives a valid `CONDUCTOR_ANNOUNCE` from another board, it actively gives up conductorship and becomes a musician. In practical testing, we did not observe any obvious problem or bug caused by this behavior.

### New Conductor Responsibilities

After a successful claim, the new conductor takes over the functions previously associated with the old conductor:

- It can start and stop the melody.
- It can change tempo.
- It can change key.
- It can reset tempo and key to default values.
- It can announce the current conductor state to the network.
- It can print tempo periodically when periodic printing is enabled.

The new conductor also sends the current key, tempo, and play/stop state after announcing itself. This keeps musicians aligned with the new conductor state.

### Previous Conductor Reconfiguration

When a board receives a `CONDUCTOR_ANNOUNCE` from another node, it calls the musician reconfiguration path. If the board was previously conductor, it changes its local role to musician and prints: `Conductorship Void`

### Phenomenon

The conductor change does not reset the melody. The note sequence is controlled by the token, not by the conductor alone. During a conductor change, the boards keep their current token state. The key and tempo are stored in the application state and are also broadcast by the new conductor after it claims conductorship. This means that the new conductor can take over without forcing the melody to restart from the beginning.

### What Worked Well

The design reuses the conductor announcement mechanism from Problem 1. This kept the protocol small and avoided adding a separate election message just for manual conductor changes.

### Challenges

One challenge was making sure that all boards update their role consistently after a conductor announcement. Every board must agree on the same `conductor_id`; otherwise, musician boards might ignore valid conductor commands. Through testing, we did not notice any boards disagreeing with the conductor board ID.

### What We Learned

We learned that conductorship should be treated as shared state instead of only a local mode. When a new conductor is announced, every board must update its role and conductor ID consistently, otherwise the same CAN command can be interpreted differently by different boards.

### Contribution

Contribution from group member 1 (Jueliang Guo, 100%):
Contribution from group member 2 (Wenqi Zhao, 100%):

## Problem 3: The Sound of Silence

### Design Overview

In problem 3, we need to detect silent failures of musician boards. A silent-failed board stops playing and stops participating in the protocol. The other boards must detect the loss and continue the melody without audible gaps.

Our solution uses passive backup. No backup board plays the same note at the same time as the originally assigned board. Instead, other boards wait to detect that the expected board has failed. Only after detection is the failed node removed from the active membership, ranks are recalculated, and another board plays the note.

### Passive Backup

The implementation does not use active backup. At any time, only the board whose rank matches the current token is allowed to play.

If the assigned board fails silently, no other board immediately plays the same note in parallel. Instead, the other boards run watchdog timers. When a watchdog expires, the detecting board marks the expected node as failed and updates the membership. After removing the failed node, the note responsibility is recalculated using: `note_index % active_count`

If the recalculation assigns the current note to the detecting board, that board plays the note.

### Membership Tracking

The conductor decides network membership using the same information that all boards maintain:

- `known_nodes`: all nodes that have ever been observed.
- `active_nodes`: nodes currently considered available for playback.
- `pending_join_nodes`: nodes that have reappeared during playback and are waiting to join cleanly.

When a board is detected as failed, it is removed from `active_nodes` but kept in `known_nodes`. This allows the membership display to show it as silent rather than forgetting the node completely.

When a board rejoins while the melody is playing, it is first placed into `pending_join_nodes`. Pending joins are applied at a token boundary. This avoids changing `active_count` in the middle of a note and reduces the risk that different boards calculate different ranks for the same token.

The current membership can be printed using: `m`. The current conductor is also marked with:
`<conductor>`

### Silent Failure Detection

During playback, the board that plays the current note sends heartbeat messages. Boards use watchdog timers to detect if the expected note holder stops responding.

The implementation sends note heartbeats every 100 ms while a note is being played. If the expected node is not heard before a watchdog expires, the node is considered failed.

The watchdog delay is staggered so that not all backup boards react at the same time: `watchdog_delay = 400 ms + backup_order * 200 ms`

The first backup therefore reacts after about 400 ms, the second after about 600 ms, and so on. This keeps recovery below the required one-second bound while reducing the chance that several boards take over simultaneously.

When a node is detected as failed, the detecting board sends: `ANNOUNCE_NODE_FAILURE`

Other boards process this message and remove the failed node from their active membership list.

### Failure Mode F1

Failure Mode F1 is a manually controlled silent failure.

A board enters F1 using: `f1`. When it enters silent failure, it stops local playback, stops heartbeat/watchdog activity, mutes the tone generator, and prints: `Silent Failure`

The commands used to leave F1 are `f1` or `z`, which leave silent failure explicitly. When the board leaves silent failure, it prints: `Leave Silent Failure`

### Failure Mode F2

Failure Mode F2 is an automatically recovering silent failure. A board enters F2 using: `f2`

After entering F2, the board schedules an automatic recovery after a random delay in the range 5 to 10 seconds. When the timer expires, the board leaves silent failure, prints: `Leave Silent Failure` and starts discovery to rejoin the active network.

### Failure Mode F3

Failure Mode F3 represents a lost CAN connection. The CAN driver is configured with transmit acknowledgement enabled, so failed transmissions can be detected.

Our implementation counts consecutive CAN send failures. After three failed transmissions, the board treats this as a CAN connection problem and enters silent failure mode F3. When entering F3, the board prints: `Silent Failure` and stops participating in playback.

When CAN reception succeeds again while the board is in F3, the board leaves silent failure, prints: `Leave Silent Failure` and starts discovery again.

### Handling Two Concurrent Musician Failures

The system can tolerate up to two concurrent silent failures on separate musician boards because membership is recalculated after each detected failure. Failed nodes are removed from `active_nodes`, but the remaining nodes continue to pass the token.

### Rejoining and Synchronization

When a silent-failed board leaves the silent failure state, it does not immediately start playing. Instead, it reruns discovery and announces its presence. If the melody is already active, other boards place the node into `pending_join_nodes`.

Pending joins are applied at a token boundary. This means the active membership changes only between note decisions, not in the middle of a note. After the joining node receives a token, it learns the current melody position from the token's note index.

After its rank is recalculated from the updated `active_nodes` list, the node knows which future notes it should play. This is how it synchronizes with the ongoing sequential form without restarting the melody.

### Arbitrary Order of F1, F2, and F3

The failure handling is based on the current state of each node, not on a fixed expected order of failures. F1, F2, and F3 all enter the same silent-failure state from the perspective of the rest of the network. The difference is only how the board enters and leaves that state.

In all cases, other boards detect the missing participant, remove it from active membership, and continue the sequential melody using the remaining active boards.

### What Worked Well

The combination of token passing and membership tracking worked well. Token passing keeps the current melody position explicit, and membership tracking determines which board is responsible for that token.

### Challenges

The main challenge was synchronizing membership changes. If one board adds or removes a node earlier than another board, the same token index could map to different ranks on different boards. The `pending_join_nodes` mechanism reduces this problem by applying joins at token boundaries.

### What We Learned

We learned that failure recovery is not only about detecting a missing board, but also about updating membership at a safe time. Applying joins at token boundaries helps all boards keep the same rank calculation and reduces the risk of duplicate or skipped notes.

### Contribution

Contribution from group member 1 (Jueliang Guo, 100%):
Contribution from group member 2 (Wenqi Zhao, 100%):

## Problem 4: Silence that Conductor

### Design Overview

In this problem, we need to handle the failure of conductor board. Our system needs to establish a new conductor automatically after the current conductor becomes silent.

During the integration test with the other groups, we observed some bugs in the cross-group behavior, so this section mainly describes our intended solution and implementation approach. When several boards all use our own software, the Problem 4 test works correctly. The main idea is to treat the failed conductor as a failed active node, remove it from the active membership, and let one remaining musician take over the conductor role according to a deterministic rule.

### Conductor Failure Detection

In our design, the conductor sends heartbeat messages periodically while it is alive. Musician boards reset a conductor watchdog when they receive a heartbeat or a valid token from the current conductor.

If the conductor watchdog expires, the musician assumes that the conductor has entered silent failure. The failed conductor is removed from `active_nodes`, kept in `known_nodes`, and announced with the same node-failure announcement mechanism used in Problem 3.

### Automatic Election

After detecting the conductor failure, all remaining active boards calculate the same sorted `active_nodes` list. the remaining musician board with the lowest active node ID is elected as the new conductor.

This rule was chosen because it is deterministic and does not require an extra election round. If all boards have the same membership view, they will choose the same new conductor. The new conductor prints: `I Am The New Conductor`

After becoming conductor, the new conductor broadcasts `CONDUCTOR_ANNOUNCE` and sends the current key, tempo, and play/stop state. It also takes over the right to process `M`, so the current membership status can still be displayed from the conductor console.

### Melody Continuity

The key, tempo, and current token index are stored locally on each board from the previously received conductor commands and tokens. Therefore, a new conductor does not need to restart the melody just to take over.

When the failed conductor is removed from `active_nodes`, ranks are recalculated. The current note responsibility is then recalculated using the same token rule as before: `note_index % active_count`

If the new conductor is responsible for the current token after recalculation, it plays the note and continues token passing. Otherwise, the responsible musician continues the melody.

### Old Conductor Recovery

If the old conductor enters silent failure, it prints: `Conductorship Void Due To Failure`

It also prints: `Silent Failure`

When the old conductor leaves silent failure, it does not automatically reclaim conductorship. Instead, it switches to musician mode and prints: `I Now Join As A Musician`

Then it reruns discovery and rejoins the network like any other recovered node.

### Dead Network Recovery

If all boards enter silent failure one by one, the network becomes dead and no melody is played. When the first board leaves silent failure, it runs discovery. If no other active conductor is found during the discovery window, that board becomes conductor and starts the melody again from note 0.

### Challenges

The main challenge in Problem 4 was keeping all boards consistent after the conductor disappears. If different boards detect the conductor failure at slightly different times, they may temporarily have different `active_nodes` lists and therefore choose different new conductors.

Another challenge was cross-group compatibility. Even though the meta-group used the same CAN message IDs, small differences in timing, failure detection, heartbeat handling, or recovery behavior could still create bugs during integration with the other groups.

In our own software, the election rule and watchdog timing worked when several boards ran the same implementation. The remaining difficulty was making the same behavior robust when different groups' implementations interacted on the same CAN bus.

### Integration Result

The Problem 4 integration test with the other groups exposed some bugs in the cross-group behavior. When several boards all use our own software, the conductor failure recovery works correctly. Therefore, the design above is the solution strategy used in our implementation, while the remaining issue is related to cross-group compatibility.

### What We Learned

We learned that conductor failure recovery is more sensitive than musician failure recovery because the conductor also controls key, tempo, and membership display. We also learned that a design can work when all boards run the same software but still expose compatibility problems when different implementations interact.

### Contribution

Contribution from group member 1 (Jueliang Guo, 100%):
Contribution from group member 2 (Wenqi Zhao, 100%):
