// Copyright (c) 2023 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

'use strict';
const log = require('./log');
const { handleBlockConnected } = require('./events');
const cashaddr = require('ecashaddrjs');
const AsyncLock = require('async-lock');
const blockConnectedLock = new AsyncLock();

module.exports = {
    initializeWebsocket: async function (
        chronik,
        address,
        db,
        telegramBot,
        channelId,
        avalancheRpc,
    ) {
        // Subscribe to chronik websocket
        const ws = chronik.ws({
            onMessage: async msg => {
                await module.exports.parseWebsocketMessage(
                    chronik,
                    db,
                    telegramBot,
                    channelId,
                    avalancheRpc,
                    msg,
                );
            },
        });
        // Wait for WS to be connected:
        await ws.waitForOpen();
        log(`Connected to websocket`);
        const { type, hash } = cashaddr.decode(address, true);
        ws.subscribe(type, hash);
        return ws;
    },
    parseWebsocketMessage: async function (
        chronik,
        db,
        telegramBot,
        channelId,
        avalancheRpc,
        wsMsg = { type: 'BlockConnected' },
    ) {
        log(`parseWebsocketMessage called on`, wsMsg);
        // Determine type of tx
        const { type } = wsMsg;
        log(`msg type: ${type}`);
        // type can be AddedToMempool, BlockConnected, or Confirmed
        // For now, we are only interested in "Confirmed", as only these are valid
        // We will want to look at AddedToMempool to process pending alias registrations later

        switch (type) {
            case 'BlockConnected': {
                log(`New block found: ${wsMsg.blockHash}`);

                return blockConnectedLock
                    .acquire('handleBlockConnected', async function () {
                        return await handleBlockConnected(
                            chronik,
                            db,
                            telegramBot,
                            channelId,
                            avalancheRpc,
                            wsMsg.blockHash,
                        );
                    })
                    .then(
                        result => {
                            // lock released with no error
                            return result;
                        },
                        error => {
                            // lock released with error thrown by handleBlockConnected()
                            log(
                                `Error in handleBlockConnected called by ${wsMsg.blockHash}`,
                                error,
                            );
                            // TODO notify admin
                            return false;
                        },
                    );
            }
            case 'AddedToMempool':
                log(`New tx: ${wsMsg.txid}`);
                break;
            case 'Confirmed':
                log(`New confirmed tx: ${wsMsg.txid}`);
                break;
            default:
                log(`New websocket message of unknown type:`, wsMsg);
        }
    },
};
