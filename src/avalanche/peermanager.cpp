// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <avalanche/peermanager.h>

#include <avalanche/avalanche.h>
#include <avalanche/delegation.h>
#include <avalanche/validation.h>
#include <random.h>
#include <scheduler.h>
#include <validation.h> // For ChainstateManager

#include <algorithm>
#include <cassert>

namespace avalanche {
bool PeerManager::addNode(NodeId nodeid, const ProofId &proofid) {
    auto &pview = peers.get<by_proofid>();
    auto it = pview.find(proofid);
    if (it == pview.end()) {
        // If the node exists, it is actually updating its proof to an unknown
        // one. In this case we need to remove it so it is not both active and
        // pending at the same time.
        removeNode(nodeid);
        pendingNodes.emplace(proofid, nodeid);
        return false;
    }

    return addOrUpdateNode(peers.project<0>(it), nodeid);
}

bool PeerManager::addOrUpdateNode(const PeerSet::iterator &it, NodeId nodeid) {
    assert(it != peers.end());

    const PeerId peerid = it->peerid;

    auto nit = nodes.find(nodeid);
    if (nit == nodes.end()) {
        if (!nodes.emplace(nodeid, peerid).second) {
            return false;
        }
    } else {
        const PeerId oldpeerid = nit->peerid;
        if (!nodes.modify(nit, [&](Node &n) { n.peerid = peerid; })) {
            return false;
        }

        // We actually have this node already, we need to update it.
        bool success = removeNodeFromPeer(peers.find(oldpeerid));
        assert(success);
    }

    bool success = addNodeToPeer(it);
    assert(success);

    // If the added node was in the pending set, remove it
    pendingNodes.get<by_nodeid>().erase(nodeid);

    return true;
}

bool PeerManager::addNodeToPeer(const PeerSet::iterator &it) {
    assert(it != peers.end());
    return peers.modify(it, [&](Peer &p) {
        if (p.node_count++ > 0) {
            // We are done.
            return;
        }

        // We need to allocate this peer.
        p.index = uint32_t(slots.size());
        const uint32_t score = p.getScore();
        const uint64_t start = slotCount;
        slots.emplace_back(start, score, it->peerid);
        slotCount = start + score;

        // Add to our allocated score when we allocate a new peer in the slots
        connectedPeersScore += score;
    });
}

bool PeerManager::removeNode(NodeId nodeid) {
    if (pendingNodes.get<by_nodeid>().erase(nodeid) > 0) {
        // If this was a pending node, there is nothing else to do.
        return true;
    }

    auto it = nodes.find(nodeid);
    if (it == nodes.end()) {
        return false;
    }

    const PeerId peerid = it->peerid;
    nodes.erase(it);

    // Keep the track of the reference count.
    bool success = removeNodeFromPeer(peers.find(peerid));
    assert(success);

    return true;
}

bool PeerManager::removeNodeFromPeer(const PeerSet::iterator &it,
                                     uint32_t count) {
    // It is possible for nodes to be dangling. If there was an inflight query
    // when the peer gets removed, the node was not erased. In this case there
    // is nothing to do.
    if (it == peers.end()) {
        return true;
    }

    assert(count <= it->node_count);
    if (count == 0) {
        // This is a NOOP.
        return false;
    }

    const uint32_t new_count = it->node_count - count;
    if (!peers.modify(it, [&](Peer &p) { p.node_count = new_count; })) {
        return false;
    }

    if (new_count > 0) {
        // We are done.
        return true;
    }

    // There are no more nodes left, we need to clean up. Subtract allocated
    // score and remove from slots.
    const size_t i = it->index;
    assert(i < slots.size());
    assert(connectedPeersScore >= slots[i].getScore());
    connectedPeersScore -= slots[i].getScore();

    if (i + 1 == slots.size()) {
        slots.pop_back();
        slotCount = slots.empty() ? 0 : slots.back().getStop();
    } else {
        fragmentation += slots[i].getScore();
        slots[i] = slots[i].withPeerId(NO_PEER);
    }

    return true;
}

bool PeerManager::updateNextRequestTime(NodeId nodeid, TimePoint timeout) {
    auto it = nodes.find(nodeid);
    if (it == nodes.end()) {
        return false;
    }

    return nodes.modify(it, [&](Node &n) { n.nextRequestTime = timeout; });
}

bool PeerManager::latchAvaproofsSent(NodeId nodeid) {
    auto it = nodes.find(nodeid);
    if (it == nodes.end()) {
        return false;
    }

    return !it->avaproofsSent &&
           nodes.modify(it, [&](Node &n) { n.avaproofsSent = true; });
}

static bool isImmatureState(const ProofValidationState &state) {
    return state.GetResult() == ProofValidationResult::IMMATURE_UTXO;
}

bool PeerManager::updateNextPossibleConflictTime(
    PeerId peerid, const std::chrono::seconds &nextTime) {
    auto it = peers.find(peerid);
    if (it == peers.end()) {
        // No such peer
        return false;
    }

    // Make sure we don't move the time in the past.
    peers.modify(it, [&](Peer &p) {
        p.nextPossibleConflictTime =
            std::max(p.nextPossibleConflictTime, nextTime);
    });

    return it->nextPossibleConflictTime == nextTime;
}

bool PeerManager::setFinalized(PeerId peerid) {
    auto it = peers.find(peerid);
    if (it == peers.end()) {
        // No such peer
        return false;
    }

    peers.modify(it, [&](Peer &p) { p.hasFinalized = true; });

    return true;
}

template <typename ProofContainer>
void PeerManager::moveToConflictingPool(const ProofContainer &proofs) {
    auto &peersView = peers.get<by_proofid>();
    for (const ProofRef &proof : proofs) {
        auto it = peersView.find(proof->getId());
        if (it != peersView.end()) {
            removePeer(it->peerid);
        }

        conflictingProofPool.addProofIfPreferred(proof);
    }
}

bool PeerManager::registerProof(const ProofRef &proof,
                                ProofRegistrationState &registrationState,
                                RegistrationMode mode) {
    assert(proof);

    const ProofId &proofid = proof->getId();

    auto invalidate = [&](ProofRegistrationResult result,
                          const std::string &message) {
        return registrationState.Invalid(
            result, message, strprintf("proofid: %s", proofid.ToString()));
    };

    if ((mode != RegistrationMode::FORCE_ACCEPT ||
         !isInConflictingPool(proofid)) &&
        exists(proofid)) {
        // In default mode, we expect the proof to be unknown, i.e. in none of
        // the pools.
        // In forced accept mode, the proof can be in the conflicting pool.
        return invalidate(ProofRegistrationResult::ALREADY_REGISTERED,
                          "proof-already-registered");
    }

    if (danglingProofIds.contains(proofid) &&
        pendingNodes.count(proofid) == 0) {
        // Don't attempt to register a proof that we already evicted because it
        // was dangling, but rather attempt to retrieve an associated node.
        needMoreNodes = true;
        return invalidate(ProofRegistrationResult::DANGLING, "dangling-proof");
    }

    // Check the proof's validity.
    ProofValidationState validationState;
    if (!WITH_LOCK(cs_main, return proof->verify(stakeUtxoDustThreshold,
                                                 chainman, validationState))) {
        if (isImmatureState(validationState)) {
            immatureProofPool.addProofIfPreferred(proof);
            if (immatureProofPool.countProofs() >
                AVALANCHE_MAX_IMMATURE_PROOFS) {
                // Adding this proof exceeds the immature pool limit, so evict
                // the lowest scoring proof.
                immatureProofPool.removeProof(
                    immatureProofPool.getLowestScoreProof()->getId());
            }

            return invalidate(ProofRegistrationResult::IMMATURE,
                              "immature-proof");
        }

        if (validationState.GetResult() ==
            ProofValidationResult::MISSING_UTXO) {
            return invalidate(ProofRegistrationResult::MISSING_UTXO,
                              "utxo-missing-or-spent");
        }

        // Reject invalid proof.
        return invalidate(ProofRegistrationResult::INVALID, "invalid-proof");
    }

    auto now = GetTime<std::chrono::seconds>();
    auto nextCooldownTimePoint =
        now + std::chrono::seconds(
                  gArgs.GetArg("-avalancheconflictingproofcooldown",
                               AVALANCHE_DEFAULT_CONFLICTING_PROOF_COOLDOWN));

    ProofPool::ConflictingProofSet conflictingProofs;
    switch (validProofPool.addProofIfNoConflict(proof, conflictingProofs)) {
        case ProofPool::AddProofStatus::REJECTED: {
            if (mode != RegistrationMode::FORCE_ACCEPT) {
                auto bestPossibleConflictTime = std::chrono::seconds(0);
                auto &pview = peers.get<by_proofid>();
                for (auto &conflictingProof : conflictingProofs) {
                    auto it = pview.find(conflictingProof->getId());
                    assert(it != pview.end());

                    // Search the most recent time over the peers
                    bestPossibleConflictTime = std::max(
                        bestPossibleConflictTime, it->nextPossibleConflictTime);

                    updateNextPossibleConflictTime(it->peerid,
                                                   nextCooldownTimePoint);
                }

                if (bestPossibleConflictTime > now) {
                    // Cooldown not elapsed, reject the proof.
                    return invalidate(
                        ProofRegistrationResult::COOLDOWN_NOT_ELAPSED,
                        "cooldown-not-elapsed");
                }

                // If proof replacement is enabled, give the proof a chance to
                // replace the conflicting ones.
                if (gArgs.GetBoolArg(
                        "-enableavalancheproofreplacement",
                        AVALANCHE_DEFAULT_PROOF_REPLACEMENT_ENABLED)) {
                    if (validProofPool.addProofIfPreferred(proof)) {
                        // If we have overridden other proofs due to conflict,
                        // remove the peers and attempt to move them to the
                        // conflicting pool.
                        moveToConflictingPool(conflictingProofs);

                        // Replacement is successful, continue to peer creation
                        break;
                    }
                }

                // Not the preferred proof, or replacement is not enabled
                return conflictingProofPool.addProofIfPreferred(proof) ==
                               ProofPool::AddProofStatus::REJECTED
                           ? invalidate(ProofRegistrationResult::REJECTED,
                                        "rejected-proof")
                           : invalidate(ProofRegistrationResult::CONFLICTING,
                                        "conflicting-utxos");
            }

            conflictingProofPool.removeProof(proofid);

            // Move the conflicting proofs from the valid pool to the
            // conflicting pool
            moveToConflictingPool(conflictingProofs);

            auto status = validProofPool.addProofIfNoConflict(proof);
            assert(status == ProofPool::AddProofStatus::SUCCEED);

            break;
        }
        case ProofPool::AddProofStatus::DUPLICATED:
            // If the proof was already in the pool, don't duplicate the peer.
            return invalidate(ProofRegistrationResult::ALREADY_REGISTERED,
                              "proof-already-registered");
        case ProofPool::AddProofStatus::SUCCEED:
            break;

            // No default case, so the compiler can warn about missing cases
    }

    // At this stage we are going to create a peer so the proof should never
    // exist in the conflicting pool, but use belt and suspenders.
    conflictingProofPool.removeProof(proofid);

    // New peer means new peerid!
    const PeerId peerid = nextPeerId++;

    // We have no peer for this proof, time to create it.
    auto inserted = peers.emplace(peerid, proof, nextCooldownTimePoint);
    assert(inserted.second);

    auto insertedRadixTree = shareableProofs.insert(proof);
    assert(insertedRadixTree);

    // Add to our registered score when adding to the peer list
    totalPeersScore += proof->getScore();

    // If there are nodes waiting for this proof, add them
    auto &pendingNodesView = pendingNodes.get<by_proofid>();
    auto range = pendingNodesView.equal_range(proofid);

    // We want to update the nodes then remove them from the pending set. That
    // will invalidate the range iterators, so we need to save the node ids
    // first before we can loop over them.
    std::vector<NodeId> nodeids;
    nodeids.reserve(std::distance(range.first, range.second));
    std::transform(range.first, range.second, std::back_inserter(nodeids),
                   [](const PendingNode &n) { return n.nodeid; });

    for (const NodeId &nodeid : nodeids) {
        addOrUpdateNode(inserted.first, nodeid);
    }

    return true;
}

bool PeerManager::rejectProof(const ProofId &proofid, RejectionMode mode) {
    if (!exists(proofid)) {
        return false;
    }

    if (immatureProofPool.removeProof(proofid)) {
        return true;
    }

    if (mode == RejectionMode::DEFAULT &&
        conflictingProofPool.getProof(proofid)) {
        // In default mode we keep the proof in the conflicting pool
        return true;
    }

    if (mode == RejectionMode::INVALIDATE &&
        conflictingProofPool.removeProof(proofid)) {
        // In invalidate mode we remove the proof completely
        return true;
    }

    auto &pview = peers.get<by_proofid>();
    auto it = pview.find(proofid);
    assert(it != pview.end());

    const ProofRef proof = it->proof;

    if (!removePeer(it->peerid)) {
        return false;
    }

    // If there was conflicting proofs, attempt to pull them back
    for (const SignedStake &ss : proof->getStakes()) {
        const ProofRef conflictingProof =
            conflictingProofPool.getProof(ss.getStake().getUTXO());
        if (!conflictingProof) {
            continue;
        }

        conflictingProofPool.removeProof(conflictingProof->getId());
        registerProof(conflictingProof);
    }

    if (mode == RejectionMode::DEFAULT) {
        conflictingProofPool.addProofIfPreferred(proof);
    }

    return true;
}

void PeerManager::cleanupDanglingProofs(const ProofRef &localProof) {
    const auto now = GetTime<std::chrono::seconds>();

    std::vector<ProofId> newlyDanglingProofIds;
    for (const Peer &peer : peers) {
        // If the peer is not our local proof, has been registered for some
        // time and has no node attached, discard it.
        if ((!localProof || peer.getProofId() != localProof->getId()) &&
            peer.node_count == 0 &&
            (peer.registration_time + Peer::DANGLING_TIMEOUT) <= now) {
            newlyDanglingProofIds.push_back(peer.getProofId());
        }
    }

    for (const ProofId &proofid : newlyDanglingProofIds) {
        rejectProof(proofid, RejectionMode::INVALIDATE);
        danglingProofIds.insert(proofid);
        LogPrint(
            BCLog::AVALANCHE,
            "Proof dropped for dangling too long (no connected node): %s\n",
            proofid.GetHex());
    }

    // If we have dangling proof, this is a good indicator that we need to
    // request more nodes from our peers.
    needMoreNodes = !newlyDanglingProofIds.empty();
}

NodeId PeerManager::selectNode() {
    for (int retry = 0; retry < SELECT_NODE_MAX_RETRY; retry++) {
        const PeerId p = selectPeer();

        // If we cannot find a peer, it may be due to the fact that it is
        // unlikely due to high fragmentation, so compact and retry.
        if (p == NO_PEER) {
            compact();
            continue;
        }

        // See if that peer has an available node.
        auto &nview = nodes.get<next_request_time>();
        auto it = nview.lower_bound(boost::make_tuple(p, TimePoint()));
        if (it != nview.end() && it->peerid == p &&
            it->nextRequestTime <= std::chrono::steady_clock::now()) {
            return it->nodeid;
        }
    }

    // We failed to find a node to query, flag this so we can request more
    needMoreNodes = true;

    return NO_NODE;
}

std::unordered_set<ProofRef, SaltedProofHasher> PeerManager::updatedBlockTip() {
    std::vector<ProofId> invalidProofIds;
    std::vector<ProofRef> newImmatures;

    {
        LOCK(cs_main);

        for (const auto &p : peers) {
            ProofValidationState state;
            if (!p.proof->verify(stakeUtxoDustThreshold, chainman, state)) {
                if (isImmatureState(state)) {
                    newImmatures.push_back(p.proof);
                }
                invalidProofIds.push_back(p.getProofId());
            }
        }
    }

    // Remove the invalid proofs before the immature rescan. This makes it
    // possible to pull back proofs with utxos that conflicted with these
    // invalid proofs.
    for (const ProofId &invalidProofId : invalidProofIds) {
        rejectProof(invalidProofId, RejectionMode::INVALIDATE);
    }

    auto registeredProofs = immatureProofPool.rescan(*this);

    for (auto &p : newImmatures) {
        immatureProofPool.addProofIfPreferred(p);
    }

    return registeredProofs;
}

ProofRef PeerManager::getProof(const ProofId &proofid) const {
    ProofRef proof;

    forPeer(proofid, [&](const Peer &p) {
        proof = p.proof;
        return true;
    });

    if (!proof) {
        proof = conflictingProofPool.getProof(proofid);
    }

    if (!proof) {
        proof = immatureProofPool.getProof(proofid);
    }

    return proof;
}

bool PeerManager::isBoundToPeer(const ProofId &proofid) const {
    auto &pview = peers.get<by_proofid>();
    return pview.find(proofid) != pview.end();
}

bool PeerManager::isImmature(const ProofId &proofid) const {
    return immatureProofPool.getProof(proofid) != nullptr;
}

bool PeerManager::isInConflictingPool(const ProofId &proofid) const {
    return conflictingProofPool.getProof(proofid) != nullptr;
}

bool PeerManager::removePeer(const PeerId peerid) {
    auto it = peers.find(peerid);
    if (it == peers.end()) {
        return false;
    }

    // Remove all nodes from this peer.
    removeNodeFromPeer(it, it->node_count);

    auto &nview = nodes.get<next_request_time>();

    // Add the nodes to the pending set
    auto range = nview.equal_range(peerid);
    for (auto &nit = range.first; nit != range.second; ++nit) {
        pendingNodes.emplace(it->getProofId(), nit->nodeid);
    };

    // Remove nodes associated with this peer, unless their timeout is still
    // active. This ensure that we don't overquery them in case they are
    // subsequently added to another peer.
    nview.erase(nview.lower_bound(boost::make_tuple(peerid, TimePoint())),
                nview.upper_bound(boost::make_tuple(
                    peerid, std::chrono::steady_clock::now())));

    // Release UTXOs attached to this proof.
    validProofPool.removeProof(it->getProofId());

    auto removed = shareableProofs.remove(Uint256RadixKey(it->getProofId()));
    assert(removed != nullptr);

    m_unbroadcast_proofids.erase(it->getProofId());

    // Remove the peer from the PeerSet and remove its score from the registered
    // score total.
    assert(totalPeersScore >= it->getScore());
    totalPeersScore -= it->getScore();
    peers.erase(it);
    return true;
}

PeerId PeerManager::selectPeer() const {
    if (slots.empty() || slotCount == 0) {
        return NO_PEER;
    }

    const uint64_t max = slotCount;
    for (int retry = 0; retry < SELECT_PEER_MAX_RETRY; retry++) {
        size_t i = selectPeerImpl(slots, GetRand(max), max);
        if (i != NO_PEER) {
            return i;
        }
    }

    return NO_PEER;
}

uint64_t PeerManager::compact() {
    // There is nothing to compact.
    if (fragmentation == 0) {
        return 0;
    }

    std::vector<Slot> newslots;
    newslots.reserve(peers.size());

    uint64_t prevStop = 0;
    uint32_t i = 0;
    for (auto it = peers.begin(); it != peers.end(); it++) {
        if (it->node_count == 0) {
            continue;
        }

        newslots.emplace_back(prevStop, it->getScore(), it->peerid);
        prevStop = slots[i].getStop();
        if (!peers.modify(it, [&](Peer &p) { p.index = i++; })) {
            return 0;
        }
    }

    slots = std::move(newslots);

    const uint64_t saved = slotCount - prevStop;
    slotCount = prevStop;
    fragmentation = 0;

    return saved;
}

bool PeerManager::verify() const {
    uint64_t prevStop = 0;
    uint32_t scoreFromSlots = 0;
    for (size_t i = 0; i < slots.size(); i++) {
        const Slot &s = slots[i];

        // Slots must be in correct order.
        if (s.getStart() < prevStop) {
            return false;
        }

        prevStop = s.getStop();

        // If this is a dead slot, then nothing more needs to be checked.
        if (s.getPeerId() == NO_PEER) {
            continue;
        }

        // We have a live slot, verify index.
        auto it = peers.find(s.getPeerId());
        if (it == peers.end() || it->index != i) {
            return false;
        }

        // Accumulate score across slots
        scoreFromSlots += slots[i].getScore();
    }

    // Score across slots must be the same as our allocated score
    if (scoreFromSlots != connectedPeersScore) {
        return false;
    }

    uint32_t scoreFromAllPeers = 0;
    uint32_t scoreFromPeersWithNodes = 0;

    std::unordered_set<COutPoint, SaltedOutpointHasher> peersUtxos;
    for (const auto &p : peers) {
        // Accumulate the score across peers to compare with total known score
        scoreFromAllPeers += p.getScore();

        // A peer should have a proof attached
        if (!p.proof) {
            return false;
        }

        // Check proof pool consistency
        for (const auto &ss : p.proof->getStakes()) {
            const COutPoint &outpoint = ss.getStake().getUTXO();
            auto proof = validProofPool.getProof(outpoint);

            if (!proof) {
                // Missing utxo
                return false;
            }
            if (proof != p.proof) {
                // Wrong proof
                return false;
            }

            if (!peersUtxos.emplace(outpoint).second) {
                // Duplicated utxo
                return false;
            }
        }

        // Count node attached to this peer.
        const auto count_nodes = [&]() {
            size_t count = 0;
            auto &nview = nodes.get<next_request_time>();
            auto begin =
                nview.lower_bound(boost::make_tuple(p.peerid, TimePoint()));
            auto end =
                nview.upper_bound(boost::make_tuple(p.peerid + 1, TimePoint()));

            for (auto it = begin; it != end; ++it) {
                count++;
            }

            return count;
        };

        if (p.node_count != count_nodes()) {
            return false;
        }

        // If there are no nodes attached to this peer, then we are done.
        if (p.node_count == 0) {
            continue;
        }

        scoreFromPeersWithNodes += p.getScore();
        // The index must point to a slot refering to this peer.
        if (p.index >= slots.size() || slots[p.index].getPeerId() != p.peerid) {
            return false;
        }

        // If the score do not match, same thing.
        if (slots[p.index].getScore() != p.getScore()) {
            return false;
        }

        // Check the proof is in the radix tree
        if (shareableProofs.get(p.getProofId()) == nullptr) {
            return false;
        }
    }

    // Check our accumulated scores against our registred and allocated scores
    if (scoreFromAllPeers != totalPeersScore) {
        return false;
    }
    if (scoreFromPeersWithNodes != connectedPeersScore) {
        return false;
    }

    // We checked the utxo consistency for all our peers utxos already, so if
    // the pool size differs from the expected one there are dangling utxos.
    if (validProofPool.size() != peersUtxos.size()) {
        return false;
    }

    // Check there is no dangling proof in the radix tree
    return shareableProofs.forEachLeaf([&](RCUPtr<const Proof> pLeaf) {
        return isBoundToPeer(pLeaf->getId());
    });
}

PeerId selectPeerImpl(const std::vector<Slot> &slots, const uint64_t slot,
                      const uint64_t max) {
    assert(slot <= max);

    size_t begin = 0, end = slots.size();
    uint64_t bottom = 0, top = max;

    // Try to find the slot using dichotomic search.
    while ((end - begin) > 8) {
        // The slot we picked in not allocated.
        if (slot < bottom || slot >= top) {
            return NO_PEER;
        }

        // Guesstimate the position of the slot.
        size_t i = begin + ((slot - bottom) * (end - begin) / (top - bottom));
        assert(begin <= i && i < end);

        // We have a match.
        if (slots[i].contains(slot)) {
            return slots[i].getPeerId();
        }

        // We undershooted.
        if (slots[i].precedes(slot)) {
            begin = i + 1;
            if (begin >= end) {
                return NO_PEER;
            }

            bottom = slots[begin].getStart();
            continue;
        }

        // We overshooted.
        if (slots[i].follows(slot)) {
            end = i;
            top = slots[end].getStart();
            continue;
        }

        // We have an unalocated slot.
        return NO_PEER;
    }

    // Enough of that nonsense, let fallback to linear search.
    for (size_t i = begin; i < end; i++) {
        // We have a match.
        if (slots[i].contains(slot)) {
            return slots[i].getPeerId();
        }
    }

    // We failed to find a slot, retry.
    return NO_PEER;
}

void PeerManager::addUnbroadcastProof(const ProofId &proofid) {
    // The proof should be bound to a peer
    if (isBoundToPeer(proofid)) {
        m_unbroadcast_proofids.insert(proofid);
    }
}

void PeerManager::removeUnbroadcastProof(const ProofId &proofid) {
    m_unbroadcast_proofids.erase(proofid);
}

} // namespace avalanche
