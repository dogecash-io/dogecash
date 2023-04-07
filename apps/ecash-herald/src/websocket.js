// Copyright (c) 2023 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

'use strict';
const config = require('../config');
const cashaddr = require('ecashaddrjs');
const { parseBlock, getBlockTgMessage } = require('./parse');

module.exports = {
    initializeWebsocket: async function (
        chronik,
        address,
        telegramBot,
        channelId,
    ) {
        // Subscribe to chronik websocket
        const ws = chronik.ws({
            onMessage: async msg => {
                await module.exports.parseWebsocketMessage(
                    chronik,
                    msg,
                    telegramBot,
                    channelId,
                );
            },
        });
        // Wait for WS to be connected:
        await ws.waitForOpen();
        console.log(`Connected to websocket`);
        // Subscribe to scripts (on Lotus, current ABC payout address):
        // Will give a message on avg every 2 minutes
        const { type, hash } = cashaddr.decode(address, true);
        ws.subscribe(type, hash);
        return ws;
    },
    parseWebsocketMessage: async function (
        chronik,
        wsMsg,
        telegramBot,
        channelId,
    ) {
        console.log(`New chronik websocket message`, wsMsg);
        // Determine type of tx
        const { type } = wsMsg;

        // type can be AddedToMempool, BlockConnected, or Confirmed

        switch (type) {
            case 'BlockConnected': {
                // Here is where you will send a telegram msg
                // Construct your Telegram message in markdown
                const { blockHash } = wsMsg;

                // Get some info about this block
                let blockDetails;
                let parsedBlock;
                let generatedTgMsg;
                try {
                    blockDetails = await chronik.block(blockHash);
                    parsedBlock = parseBlock(blockDetails);
                    generatedTgMsg = getBlockTgMessage(parsedBlock);
                } catch (err) {
                    blockDetails = false;
                    console.log(`Error in chronik.block(${blockHash})`, err);
                }

                // Construct your Telegram message in markdown
                const tgMsg = blockDetails
                    ? generatedTgMsg
                    : `New Block Found\n` +
                      `\n` +
                      `${blockHash}\n` +
                      `\n` +
                      `[explorer](${config.blockExplorer}/block/${blockHash})`;

                try {
                    return await telegramBot.sendMessage(
                        channelId,
                        tgMsg,
                        config.tgMsgOptions,
                    );
                } catch (err) {
                    console.log(
                        `Error in telegramBot.sendMessage(channelId=${channelId}, msg=${tgMsg}, options=${config.tgMsgOptions})`,
                        err,
                    );
                }
                return false;
            }
            default:
                console.log(`New websocket message of unknown type:`, wsMsg);
                return false;
        }
    },
};