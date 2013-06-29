#include "DefaultBtMessageDispatcher.h"

#include <cassert>

#include <cppunit/extensions/HelperMacros.h>

#include "util.h"
#include "Exception.h"
#include "MockPieceStorage.h"
#include "MockPeerStorage.h"
#include "MockBtMessage.h"
#include "MockBtMessageFactory.h"
#include "prefs.h"
#include "BtCancelSendingPieceEvent.h"
#include "BtHandshakeMessage.h"
#include "Option.h"
#include "RequestGroupMan.h"
#include "ServerStatMan.h"
#include "RequestGroup.h"
#include "DownloadContext.h"
#include "bittorrent_helper.h"
#include "PeerConnection.h"

namespace aria2 {

class DefaultBtMessageDispatcherTest:public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(DefaultBtMessageDispatcherTest);
  CPPUNIT_TEST(testAddMessage);
  CPPUNIT_TEST(testSendMessages);
  CPPUNIT_TEST(testSendMessages_underUploadLimit);
  // See the comment on the definition
  //CPPUNIT_TEST(testSendMessages_overUploadLimit);
  CPPUNIT_TEST(testDoCancelSendingPieceAction);
  CPPUNIT_TEST(testCheckRequestSlotAndDoNecessaryThing);
  CPPUNIT_TEST(testCheckRequestSlotAndDoNecessaryThing_timeout);
  CPPUNIT_TEST(testCheckRequestSlotAndDoNecessaryThing_completeBlock);
  CPPUNIT_TEST(testCountOutstandingRequest);
  CPPUNIT_TEST(testIsOutstandingRequest);
  CPPUNIT_TEST(testGetOutstandingRequest);
  CPPUNIT_TEST(testRemoveOutstandingRequest);
  CPPUNIT_TEST_SUITE_END();
private:
  std::shared_ptr<DownloadContext> dctx_;
  std::shared_ptr<Peer> peer;
  std::shared_ptr<DefaultBtMessageDispatcher> btMessageDispatcher;
  std::shared_ptr<MockPeerStorage> peerStorage;
  std::shared_ptr<MockPieceStorage> pieceStorage;
  std::shared_ptr<MockBtMessageFactory> messageFactory_;
  std::shared_ptr<RequestGroupMan> rgman_;
  std::shared_ptr<Option> option_;
  std::shared_ptr<RequestGroup> rg_;
public:
  void tearDown() {}

  void testAddMessage();
  void testSendMessages();
  void testSendMessages_underUploadLimit();
  void testSendMessages_overUploadLimit();
  void testDoCancelSendingPieceAction();
  void testCheckRequestSlotAndDoNecessaryThing();
  void testCheckRequestSlotAndDoNecessaryThing_timeout();
  void testCheckRequestSlotAndDoNecessaryThing_completeBlock();
  void testCountOutstandingRequest();
  void testIsOutstandingRequest();
  void testGetOutstandingRequest();
  void testRemoveOutstandingRequest();

  class MockBtMessage2 : public MockBtMessage {
  private:
    bool onQueuedCalled;
    bool sendCalled;
    bool doCancelActionCalled;
  public:
    std::string type;
  public:
    MockBtMessage2():onQueuedCalled(false),
                     sendCalled(false),
                     doCancelActionCalled(false)
    {}

    virtual ~MockBtMessage2() {}

    virtual void onQueued() {
      onQueuedCalled = true;
    }

    bool isOnQueuedCalled() const {
      return onQueuedCalled;
    }

    virtual void send() {
      sendCalled = true;
    }

    bool isSendCalled() const {
      return sendCalled;
    }

    virtual void onCancelSendingPieceEvent
    (const BtCancelSendingPieceEvent& event)
    {
      doCancelActionCalled = true;
    }

    bool isDoCancelActionCalled() const {
      return doCancelActionCalled;
    }
  };

  class MockPieceStorage2 : public MockPieceStorage {
  private:
    std::shared_ptr<Piece> piece;
  public:
    virtual std::shared_ptr<Piece> getPiece(size_t index) {
      return piece;
    }

    void setPiece(const std::shared_ptr<Piece>& piece) {
      this->piece = piece;
    }
  };

  class MockBtMessageFactory2 : public MockBtMessageFactory {
  public:
    virtual std::shared_ptr<BtMessage>
    createCancelMessage(size_t index, int32_t begin, int32_t length) {
      std::shared_ptr<MockBtMessage2> btMsg(new MockBtMessage2());
      btMsg->type = "cancel";
      return btMsg;
    }
  };

  void setUp() {
    option_.reset(new Option());
    option_->put(PREF_DIR, ".");

    rg_.reset(new RequestGroup(GroupId::create(), option_));

    dctx_.reset(new DownloadContext());
    bittorrent::load(A2_TEST_DIR"/test.torrent", dctx_, option_);

    rg_->setDownloadContext(dctx_);

    peer.reset(new Peer("192.168.0.1", 6969));
    peer->allocateSessionResource
      (dctx_->getPieceLength(), dctx_->getTotalLength());
    peerStorage.reset(new MockPeerStorage());
    pieceStorage.reset(new MockPieceStorage());

    messageFactory_.reset(new MockBtMessageFactory2());

    rgman_.reset(new RequestGroupMan(std::vector<std::shared_ptr<RequestGroup> >(),
                                     0, option_.get()));

    btMessageDispatcher.reset(new DefaultBtMessageDispatcher());
    btMessageDispatcher->setPeer(peer);
    btMessageDispatcher->setDownloadContext(dctx_.get());
    btMessageDispatcher->setPieceStorage(pieceStorage.get());
    btMessageDispatcher->setPeerStorage(peerStorage.get());
    btMessageDispatcher->setBtMessageFactory(messageFactory_.get());
    btMessageDispatcher->setCuid(1);
    btMessageDispatcher->setRequestGroupMan(rgman_.get());
  }
};


CPPUNIT_TEST_SUITE_REGISTRATION(DefaultBtMessageDispatcherTest);

void DefaultBtMessageDispatcherTest::testAddMessage() {
  std::shared_ptr<MockBtMessage2> msg(new MockBtMessage2());
  CPPUNIT_ASSERT_EQUAL(false, msg->isOnQueuedCalled());
  btMessageDispatcher->addMessageToQueue(msg);
  CPPUNIT_ASSERT_EQUAL(true, msg->isOnQueuedCalled());
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       btMessageDispatcher->getMessageQueue().size());
}

void DefaultBtMessageDispatcherTest::testSendMessages() {
  std::shared_ptr<MockBtMessage2> msg1(new MockBtMessage2());
  msg1->setUploading(false);
  std::shared_ptr<MockBtMessage2> msg2(new MockBtMessage2());
  msg2->setUploading(false);
  btMessageDispatcher->addMessageToQueue(msg1);
  btMessageDispatcher->addMessageToQueue(msg2);
  btMessageDispatcher->sendMessagesInternal();

  CPPUNIT_ASSERT(msg1->isSendCalled());
  CPPUNIT_ASSERT(msg2->isSendCalled());
}

void DefaultBtMessageDispatcherTest::testSendMessages_underUploadLimit() {
  std::shared_ptr<MockBtMessage2> msg1(new MockBtMessage2());
  msg1->setUploading(true);
  std::shared_ptr<MockBtMessage2> msg2(new MockBtMessage2());
  msg2->setUploading(true);
  btMessageDispatcher->addMessageToQueue(msg1);
  btMessageDispatcher->addMessageToQueue(msg2);
  btMessageDispatcher->sendMessagesInternal();

  CPPUNIT_ASSERT(msg1->isSendCalled());
  CPPUNIT_ASSERT(msg2->isSendCalled());
}

// TODO Because we no longer directly use PeerStorage::calculateStat()
// and Neither RequestGroup nor RequestGroupMan can be stubbed, this
// test is commented out for now.
//
// void DefaultBtMessageDispatcherTest::testSendMessages_overUploadLimit() {
//   btMessageDispatcher->setMaxUploadSpeedLimit(100);
//   TransferStat stat;
//   stat.setUploadSpeed(150);
//   peerStorage->setStat(stat);

//   std::shared_ptr<MockBtMessage2> msg1(new MockBtMessage2());
//   msg1->setUploading(true);
//   std::shared_ptr<MockBtMessage2> msg2(new MockBtMessage2());
//   msg2->setUploading(true);
//   std::shared_ptr<MockBtMessage2> msg3(new MockBtMessage2());
//   msg3->setUploading(false);

//   btMessageDispatcher->addMessageToQueue(msg1);
//   btMessageDispatcher->addMessageToQueue(msg2);
//   btMessageDispatcher->addMessageToQueue(msg3);
//   btMessageDispatcher->sendMessagesInternal();

//   CPPUNIT_ASSERT(!msg1->isSendCalled());
//   CPPUNIT_ASSERT(!msg2->isSendCalled());
//   CPPUNIT_ASSERT(msg3->isSendCalled());

//   CPPUNIT_ASSERT_EQUAL((size_t)2,
//                     btMessageDispatcher->getMessageQueue().size());
// }

void DefaultBtMessageDispatcherTest::testDoCancelSendingPieceAction() {
  std::shared_ptr<MockBtMessage2> msg1(new MockBtMessage2());
  std::shared_ptr<MockBtMessage2> msg2(new MockBtMessage2());

  btMessageDispatcher->addMessageToQueue(msg1);
  btMessageDispatcher->addMessageToQueue(msg2);

  btMessageDispatcher->doCancelSendingPieceAction(0, 0, 0);

  CPPUNIT_ASSERT_EQUAL(true, msg1->isDoCancelActionCalled());
  CPPUNIT_ASSERT_EQUAL(true, msg2->isDoCancelActionCalled());
}

int MY_PIECE_LENGTH = 16*1024;

void DefaultBtMessageDispatcherTest::testCheckRequestSlotAndDoNecessaryThing() {
  auto piece = std::make_shared<Piece>(0, MY_PIECE_LENGTH);
  size_t index;
  CPPUNIT_ASSERT(piece->getMissingUnusedBlockIndex(index));
  CPPUNIT_ASSERT_EQUAL((size_t)0, index);

  auto pieceStorage = make_unique<MockPieceStorage2>();
  pieceStorage->setPiece(piece);

  btMessageDispatcher->setRequestTimeout(60);
  btMessageDispatcher->setPieceStorage(pieceStorage.get());
  btMessageDispatcher->addOutstandingRequest
    (make_unique<RequestSlot>(0, 0, MY_PIECE_LENGTH, 0, piece));

  btMessageDispatcher->checkRequestSlotAndDoNecessaryThing();

  CPPUNIT_ASSERT_EQUAL((size_t)0,
                       btMessageDispatcher->getMessageQueue().size());
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       btMessageDispatcher->getRequestSlots().size());
}

void DefaultBtMessageDispatcherTest::
testCheckRequestSlotAndDoNecessaryThing_timeout() {
  auto piece = std::make_shared<Piece>(0, MY_PIECE_LENGTH);
  size_t index;
  CPPUNIT_ASSERT(piece->getMissingUnusedBlockIndex(index));
  CPPUNIT_ASSERT_EQUAL((size_t)0, index);

  auto pieceStorage = make_unique<MockPieceStorage2>();
  pieceStorage->setPiece(piece);

  btMessageDispatcher->setRequestTimeout(60);
  btMessageDispatcher->setPieceStorage(pieceStorage.get());
  auto slot = make_unique<RequestSlot>(0, 0, MY_PIECE_LENGTH, 0, piece);
  // make this slot timeout
  slot->setDispatchedTime(0);
  btMessageDispatcher->addOutstandingRequest(std::move(slot));
  btMessageDispatcher->checkRequestSlotAndDoNecessaryThing();

  CPPUNIT_ASSERT_EQUAL((size_t)0,
                       btMessageDispatcher->getMessageQueue().size());
  CPPUNIT_ASSERT_EQUAL((size_t)0,
                       btMessageDispatcher->getRequestSlots().size());
  CPPUNIT_ASSERT_EQUAL(false, piece->isBlockUsed(0));
  CPPUNIT_ASSERT_EQUAL(true, peer->snubbing());
}

void DefaultBtMessageDispatcherTest::
testCheckRequestSlotAndDoNecessaryThing_completeBlock() {
  auto piece = std::make_shared<Piece>(0, MY_PIECE_LENGTH);
  piece->completeBlock(0);
  auto pieceStorage = make_unique<MockPieceStorage2>();
  pieceStorage->setPiece(piece);

  btMessageDispatcher->setRequestTimeout(60);
  btMessageDispatcher->setPieceStorage(pieceStorage.get());
  btMessageDispatcher->addOutstandingRequest
    (make_unique<RequestSlot>(0, 0, MY_PIECE_LENGTH, 0, piece));

  btMessageDispatcher->checkRequestSlotAndDoNecessaryThing();

  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       btMessageDispatcher->getMessageQueue().size());
  CPPUNIT_ASSERT_EQUAL((size_t)0,
                       btMessageDispatcher->getRequestSlots().size());
}

void DefaultBtMessageDispatcherTest::testCountOutstandingRequest() {
  btMessageDispatcher->addOutstandingRequest
    (make_unique<RequestSlot>(0, 0, MY_PIECE_LENGTH, 0));
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       btMessageDispatcher->countOutstandingRequest());
}

void DefaultBtMessageDispatcherTest::testIsOutstandingRequest() {
  btMessageDispatcher->addOutstandingRequest
    (make_unique<RequestSlot>(0, 0, MY_PIECE_LENGTH, 0));

  CPPUNIT_ASSERT(btMessageDispatcher->isOutstandingRequest(0, 0));
  CPPUNIT_ASSERT(!btMessageDispatcher->isOutstandingRequest(0, 1));
  CPPUNIT_ASSERT(!btMessageDispatcher->isOutstandingRequest(1, 0));
  CPPUNIT_ASSERT(!btMessageDispatcher->isOutstandingRequest(1, 1));
}

void DefaultBtMessageDispatcherTest::testGetOutstandingRequest() {
  btMessageDispatcher->addOutstandingRequest
    (make_unique<RequestSlot>(1, 1024, 16*1024, 10));

  CPPUNIT_ASSERT(btMessageDispatcher->getOutstandingRequest(1, 1024, 16*1024));

  CPPUNIT_ASSERT(!btMessageDispatcher->
                 getOutstandingRequest(1, 1024, 17*1024));

  CPPUNIT_ASSERT(!btMessageDispatcher->
                 getOutstandingRequest(1, 2*1024, 16*1024));

  CPPUNIT_ASSERT(!btMessageDispatcher->
                 getOutstandingRequest(2, 1024, 16*1024));
}

void DefaultBtMessageDispatcherTest::testRemoveOutstandingRequest() {
  auto piece = std::make_shared<Piece>(1, 1024*1024);
  size_t blockIndex = 0;
  CPPUNIT_ASSERT(piece->getMissingUnusedBlockIndex(blockIndex));
  uint32_t begin = blockIndex*piece->getBlockLength();
  size_t length = piece->getBlockLength(blockIndex);
  RequestSlot slot;
  btMessageDispatcher->addOutstandingRequest
    (make_unique<RequestSlot>(piece->getIndex(), begin, length, blockIndex,
                              piece));

  auto s2 = btMessageDispatcher->getOutstandingRequest(piece->getIndex(),
                                                       begin, length);
  CPPUNIT_ASSERT(s2);
  CPPUNIT_ASSERT(piece->isBlockUsed(blockIndex));

  btMessageDispatcher->removeOutstandingRequest(s2);

  auto s3 = btMessageDispatcher->getOutstandingRequest(piece->getIndex(),
                                                       begin, length);
  CPPUNIT_ASSERT(!s3);
  CPPUNIT_ASSERT(!piece->isBlockUsed(blockIndex));
}

} // namespace aria2
