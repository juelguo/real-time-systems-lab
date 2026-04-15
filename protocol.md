# Meta-Group CAN Protocol Specification

This document defines the shared CAN protocol for the collaborative part of HWA#1.
It is intended to remove ambiguity before implementation so that all boards in the
meta-group follow the same message format and the same distributed behavior.

## 1. Core Assumptions

- The network contains exactly 3 boards in total.
- Each board has a fixed `nodeId` chosen from `{1, 2, 3}`.
- The `nodeId` is fixed, but the board rank is dynamic.
- Rank is computed from the set of currently alive boards.
- Alive boards are ordered by ascending `nodeId` and assigned ranks `0..n-1`.
- At boot, boards perform startup discovery to determine which of the 3 expected boards
  are currently reachable.
- Membership is maintained dynamically using heartbeat messages.

## 2. Frame Semantics

The CAN frame fields are used as follows:

- `msgId`: message type
- `nodeId`: sender board identity
- `buff[]`: payload bytes interpreted according to `msgId`

This means:

- `msgId` is never reused to store conductor identity.
- `nodeId` always identifies the sender of the frame.
- If a message needs to identify another board, that board ID must be placed in the payload.

## 3. Message Types

### Table 1: Meta Message Protocol

| Message Type | Message ID | Sender | Payload |
| --- | ---: | --- | --- |
| `CAN_MSG_CONDUCTOR_CMD` | 1 | Conductor | `buf[0]=sub_cmd`, `buf[1]=value` |
| `CAN_MSG_DISCOVERY_PING` | 2 | Any node | No payload |
| `CAN_MSG_DISCOVERY_REPLY` | 3 | Any node | `buf[0]=nodeId` |
| `CAN_MSG_CONDUCTOR_ANNOUNCE` | 4 | New conductor | `buf[0]=conductor_nodeId` |
| `CAN_MSG_TOKEN` | 5 | Current playing node or conductor | `buf[0]=note_index` |
| `CAN_MSG_HEARTBEAT` | 6 | Any alive node | `buf[0]=current_note_index`, `buf[1]=conductor_nodeId`, `buf[2]=tempo`, `buf[3]=key` |

### Table 2: Conductor Sub-commands

| Sub-command | Value |
| --- | ---: |
| `CAN_SUB_START` | 1 |
| `CAN_SUB_STOP` | 2 |
| `CAN_SUB_KEY` | 3 |
| `CAN_SUB_TEMPO` | 4 |

## 4. Identity, Rank, and Priority

- Each board has a fixed `nodeId` in `{1, 2, 3}`.
- Rank is not fixed.
- Rank is recomputed from the currently alive boards.
- Among the boards whose status is `UP`, sort by ascending `nodeId`.
- Assign dynamic ranks starting from `0`.
- Boards whose status is `DOWN` have no active rank and may be represented with rank `-1`.

Examples:

- if alive boards are `{1}`, then node `1` has rank `0`
- if alive boards are `{1, 3}`, then node `1` has rank `0`, and node `3` has rank `1`
- if alive boards are `{1, 2, 3}`, then their ranks are `0`, `1`, and `2`

This dynamic ordering is used for:

- initial conductor selection
- tie-breaking during simultaneous conductor claims
- note ownership in the round-robin melody

## 5. Startup Discovery

### 5.1 Purpose

Startup discovery determines which of the expected boards are currently reachable before
normal collaborative playback begins.

### 5.2 Discovery Procedure

At system boot:

1. Each node initializes its local membership table.
2. Each node marks itself as `UP`.
3. Each node broadcasts `CAN_MSG_DISCOVERY_PING`.
4. Any node receiving a discovery ping responds with `CAN_MSG_DISCOVERY_REPLY`.
5. A discovery reply carries the sender's `nodeId` in `buf[0]`.

### 5.3 Discovery Timeout

- Each node waits up to `1 second` for discovery replies.
- Discovery may end earlier if all 3 boards are known.

### 5.4 Discovery Stop Condition

Discovery stops when either:

- all 3 boards have been discovered, or
- the `1 second` timeout expires

If fewer than 3 boards are discovered before timeout, the node continues with the set of
boards currently known and relies on later heartbeat traffic to update membership.

### 5.5 Duplicate Handling

- If a discovery reply is received from a `nodeId` that is already known, the reply is not
  treated as a new member.
- Instead, the node simply refreshes that member's status as `UP`.

## 6. Membership Representation

Each board maintains a fixed membership table of length 3, one slot per expected `nodeId`.

Suggested internal state per member:

- `nodeId`
- `rank`
- `status`: `UP` or `DOWN`
- `last_seen_time`
- `is_conductor`

Interpretation:

- `UP` means the board is currently believed to be alive and connected.
- `DOWN` means the board is currently believed to be unavailable due to failure or lost connection.
- `rank` is meaningful only for members whose status is `UP`.
- When membership changes, ranks are recomputed for all currently alive members.

Initial state after boot:

- self is marked `UP`
- other boards are marked `UP` only when discovered or when heartbeat is received

## 7. Heartbeat and Failure Detection

### 7.1 Heartbeat Purpose

Heartbeat messages are used for:

- membership maintenance
- failure detection
- note synchronization
- conductor awareness

### 7.2 Heartbeat Transmission

- Every alive node periodically broadcasts `CAN_MSG_HEARTBEAT`.
- Heartbeat period is `10 ms`.

### 7.3 Heartbeat Contents

`CAN_MSG_HEARTBEAT` payload:

- `buf[0] = current_note_index`
- `buf[1] = conductor_nodeId`
- `buf[2] = current_tempo`
- `buf[3] = current_key`

On receiving heartbeat from node `N`, a node:

- marks member `N` as `UP`
- updates `last_seen_time` for member `N`
- updates synchronization state using the heartbeat payload

### 7.4 Watchdog Strategy

The current implementation uses a uniform timeout check:

- each board periodically checks membership state
- a board is marked `DOWN` if no refresh has been observed for `1 second`

Design rationale:

- the specification requires failure handling within `1 second`
- the current design uses a single explicit timeout threshold of `1 second`

### 7.5 Failure Interpretation

If heartbeat from a member is not observed before its watchdog deadline:

- that member is marked `DOWN`
- the board is considered to have entered silent failure or lost connection
- note assignment and conductor logic are recomputed using only currently `UP` members
- ranks are recomputed for the remaining alive boards

## 8. Conductor Rules

### 8.1 Initial Conductor

- In the default protocol, the conductor is configured manually at startup.
- If several alive boards are present and no valid manual conductor has been established,
  the alive board with the lowest `nodeId` becomes conductor.

### 8.2 Conductor Announcement

When a board becomes conductor, it broadcasts:

- `CAN_MSG_CONDUCTOR_ANNOUNCE`
- `buf[0] = conductor_nodeId`

All boards then update their local conductor state accordingly.

### 8.3 Conductorship Claims and Tie-Breaking

If two or more boards attempt to become conductor at the same time:

- the board with the lower `nodeId` wins
- the winning board broadcasts `CAN_MSG_CONDUCTOR_ANNOUNCE`
- all other boards accept the announced conductor and switch to musician behavior

To keep the protocol deterministic, lower `nodeId` always wins. The rule "later claimant wins"
is not used.

## 9. Playback Control

Only the conductor is allowed to issue global control commands.

`CAN_MSG_CONDUCTOR_CMD` carries:

- `buf[0] = sub-command`
- `buf[1] = value`

Defined commands:

- `CAN_SUB_START`
- `CAN_SUB_STOP`
- `CAN_SUB_KEY`
- `CAN_SUB_TEMPO`

Command meanings:

- `START`: begin playback
- `STOP`: stop playback
- `KEY`: update the global key
- `TEMPO`: update the global tempo

Musician boards listen to these messages and apply the new global control state.

## 10. Note Timing and Synchronization

### 10.1 Problem Being Solved

If each board relies only on its own local timers, clock drift may cause:

- gaps between notes
- overlapping notes
- disagreement about the current position in the melody

### 10.2 Synchronization Strategy

The system uses heartbeat-assisted synchronization.

- Alive boards periodically share the current playback state through heartbeat packets.
- The current note position is distributed as `current_note_index`.
- Tempo and key are also distributed so a recovering or newly joined node can synchronize.

### 10.3 Token Message

`CAN_MSG_TOKEN` is used as an explicit round-robin note progress message.

- `buf[0] = note_index`

Purpose:

- all boards know which note of the melody is active
- if the conductor fails, the remaining nodes still know where the melody currently is
- this helps support later conductor failure handling in Problem 4
- each board can determine whether the next note belongs to itself

### 10.4 Synchronization Interpretation

When a node receives synchronization information from heartbeat or token:

- it updates its view of the current note index
- it updates its view of current conductor, tempo, and key
- for `CAN_MSG_TOKEN`, the receiver uses `note_index + 1` to determine whether the next
  note belongs to itself
- if the next note belongs to the local board, it unmutes itself
- otherwise it mutes itself

## 11. Membership-Based Note Assignment

The melody is played in round-robin order across currently alive boards.

The ownership rule is:

- compute the set of alive boards
- sort alive boards by ascending `nodeId`
- assign dynamic ranks `0..n-1`
- a note with index `k` belongs to the board whose rank is `k mod n`

Example:

- if alive members are `{1, 2, 3}`, their ranks are `{0, 1, 2}`, so note assignment cycles across `1 -> 2 -> 3 -> 1 -> ...`
- if member `2` fails and alive members are `{1, 3}`, their ranks become `{0, 1}`, so note assignment cycles across `1 -> 3 -> 1 -> 3 -> ...`

This supports passive backup behavior:

- no board duplicates another board's note during normal operation
- when a board fails, the alive-member sequence shrinks and remaining boards take over
