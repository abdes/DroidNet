//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/IntrusivePtr.h"
#include "Oxygen/OxCo/Detail/ParkingLotImpl.h"
#include "Oxygen/OxCo/Detail/Queue.h"

namespace oxygen::co {

template <typename T>
class BroadcastChannel;
template <typename T>
class ReaderContext;

namespace detail::channel {
    template <typename T>
    class MultiplexedReader;
    template <typename T>
    class BroadcastingWriter;
} // namespace detail::channel

namespace detail::channel {

    //! Internal interface for reading from a `BroadcastChannel`, not intended
    //! for public use but rather through the `ReaderContext` obtained from
    //! `BroadcastChannel<T>::ForRead().
    /*!
     \tparam T The type of the objects that can be read from the channel, always
     returned wrapped in a `std::shared_ptr`.

     A `MultiplexedReader` is created when a reader is attached to the receive
     end of the channel and manages its own message buffer. Note however, that
     while message queues are independent from each other, broadcast messages
     are shared.

     \see BroadcastChannel
     \see ReaderContext
    */
    template <typename T>
    class MultiplexedReader final
        : public ParkingLotImpl<MultiplexedReader<T>>,
          public RefCounted<MultiplexedReader<T>>,
          public IntrusiveListItem<MultiplexedReader<T>> {

    public:
        friend class BroadcastChannel<T>;
        friend class BroadcastingWriter<T>;
        friend class ReaderContext<T>;

        ~MultiplexedReader() override { channel_.RemoveReader(this); }

    private:
        explicit MultiplexedReader(BroadcastChannel<T>& channel)
            : channel_(channel)
            , buffer_(0)
        {
        }

        class ReadAwaiter : public MultiplexedReader::ParkingLotImpl::Parked {
        public:
            using Base = typename MultiplexedReader::ParkingLotImpl::Parked;

            explicit ReadAwaiter(MultiplexedReader& self)
                : Base(self)
            {
            }

            [[nodiscard]] auto await_ready() const noexcept -> bool
            {
                return !GetReader().Empty() || GetReader().Closed();
            }

            void await_suspend(Handle h)
            {
                DLOG_F(1, "    ...channel {} receive {}",
                    fmt::ptr(&GetReader().channel_), fmt::ptr(this));
                this->DoSuspend(h);
            }

            auto await_resume() -> std::shared_ptr<T>
            {
                return GetReader().TryReceive();
            }

        private:
            [[nodiscard]] auto GetReader() -> MultiplexedReader&
            {
                return static_cast<MultiplexedReader&>(Base::Object());
            }
            [[nodiscard]] auto GetReader() const -> const MultiplexedReader&
            {
                return static_cast<const MultiplexedReader&>(Base::Object());
            }
        };

        auto Receive() -> Awaitable<std::shared_ptr<T>> auto
        {
            return ReadAwaiter(*this);
        }

        auto TryReceive() -> std::shared_ptr<T>
        {
            if (buffer_.Empty()) {
                return {};
            }
            auto value = std::move(buffer_.Front());
            buffer_.PopFront();
            return value;
        }

        OXYGEN_MAKE_NON_COPYABLE(MultiplexedReader)
        OXYGEN_MAKE_NON_MOVEABLE(MultiplexedReader)

        // ReSharper disable once CppHidingFunction
        [[nodiscard]] auto Empty() const noexcept { return buffer_.Empty(); }
        [[nodiscard]] auto Size() const noexcept { return buffer_.Size(); }
        [[nodiscard]] auto Closed() const noexcept -> bool { return channel_.Closed(); }

        BroadcastChannel<T>& channel_;
        Queue<std::shared_ptr<T>> buffer_;
    };

    //! Interface for writing to a `BroadcastChannel`, cannot be created
    //! directly but is obtained from `BroadcastChannel<T>::ForWrite().
    /*!
     \tparam T The type of the objects that can be sent over the channel.
     \see BroadcastChannel
    */
    template <typename T>
    class BroadcastingWriter final : public ParkingLotImpl<BroadcastingWriter<T>> {
        friend class BroadcastChannel<T>;

    public:
        OXYGEN_MAKE_NON_COPYABLE(BroadcastingWriter)
        OXYGEN_MAKE_NON_MOVEABLE(BroadcastingWriter)

        template <typename U>
        class WriteAwaiter : public BroadcastingWriter::ParkingLotImpl::Parked {
        public:
            using Base = typename BroadcastingWriter::ParkingLotImpl::Parked;

            // NOLINTNEXTLINE(*-rvalue-reference-param-not-moved) - perfect forwarding
            WriteAwaiter(BroadcastingWriter& self, U&& data)
                : Base(self)
                , data_(std::forward<U>(data))
            {
            }

            [[nodiscard]] auto await_ready() const noexcept -> bool
            {
                return GetWriter().channel_.Closed() || (!GetWriter().channel_.bounded_ || GetWriter().channel_.Space() > 0);
            }

            void await_suspend(Handle h)
            {
                DLOG_F(1, "    ...channel {} send {}",
                    fmt::ptr(&GetWriter().channel_), fmt::ptr(this));
                this->DoSuspend(h);
            }

            auto await_resume() -> bool
            {
                return GetWriter().TrySend(std::forward<U>(data_));
            }

            using Base::await_cancel;

        private:
            [[nodiscard]] auto GetWriter() -> BroadcastingWriter&
            {
                return static_cast<BroadcastingWriter&>(Base::Object());
            }
            [[nodiscard]] auto GetWriter() const -> const BroadcastingWriter&
            {
                return static_cast<const BroadcastingWriter&>(Base::Object());
            }

            U&& data_; // NOLINT(*-avoid-const-or-ref-data-members)
        };

        template <typename U>
        auto Send(U&& value) -> Awaitable<bool> auto
        {
            return WriteAwaiter<U>(*this, std::forward<U>(value));
        }

        template <typename U>
        auto TrySend(U&& value) -> bool
        {
            if (channel_.Closed()) {
                return false;
            }

            if (channel_.bounded_ && channel_.Space() == 0) {
                return false;
            }

            // Broadcast to all readers; use a shared_ptr to avoid expensive
            // copies and to allow readers to eventually communicate together
            // via the shared event value.
            auto shared_value = std::make_shared<T>(std::forward<U>(value));
            for (auto& reader : channel_.readers_) {
                reader.buffer_.PushBack(shared_value);
                reader.UnParkOne();
            }

            return true;
        }

        void Close() { channel_.Close(); }

        //! Returns the number of objects that can be sent to the channel without blocking.
        [[nodiscard]] auto Space() const noexcept { return channel_.Space(); }

        //! Returns true if the channel is full, i.e., no more objects can be
        //! sent without blocking.
        [[nodiscard]] auto Full() const noexcept { return channel_.Full(); }

        //! Returns true if `Close()` has been called on this channel.
        [[nodiscard]] auto Closed() const noexcept { return channel_.Closed(); }

    private:
        // Only allow construction and destruction as a part of Channel; we
        // assume we can't exist as a standalone object.
        explicit BroadcastingWriter(BroadcastChannel<T>& channel)
            : channel_(channel)
        {
        }
        ~BroadcastingWriter() override = default;

        BroadcastChannel<T>& channel_;
    };

} // namespace detail::channel

//! Represented a reader attached to a `BroadcastChannel` and provides the
//! public interface for receiving messages sent over it.
/*!
 \tparam T The type of the objects that can be read from the channel, always
 returned wrapped in a `std::shared_ptr`.

 A reader is able to receive messages from a `BroadcastChannel` as long as it is
 attached to it, which will result in a dedicated message queue created for it.
 The reader can then receive messages from this queue in a non-blocking manner,
 or block until a message is available.

 The `ReaderContext` is a lightweight RAII object, obtained from
 `BroadcastChannel<T>::ForRead()`. Its lifetime defines the lifetime of the
 reader's association to the receiving end of a `BroadcastChannel`. It can be
 used temporarily to just receive a single message, or kept alive to receive
 multiple messages.

 Messages are always returned wrapped in a `std::shared_ptr`, which allows for a
 more efficient and flexible way to handle the messages, as well as to allow for
 a form of communication between readers. For example, a reader can mark the
 message as processed, causing subsequent readers to skip it, or it could
 augment the message with additional data needed for subsequent processing
 stages.

 \note No assumption should be made on the order of message dispatching to
       multiple readers attached to a channel.
*/
template <typename T>
class ReaderContext {
public:
    friend class BroadcastChannel<T>;

    ~ReaderContext() = default;

    OXYGEN_DEFAULT_COPYABLE(ReaderContext)
    OXYGEN_DEFAULT_MOVABLE(ReaderContext)

    auto Receive() -> Awaitable<std::shared_ptr<T>> auto { return reader_->Receive(); }
    auto TryReceive() -> std::shared_ptr<T> { return reader_->TryReceive(); }

    //! Returns true if this channel has no buffered objects, i.e., a call to
    //! TryReceive() will return `nullptr`.
    [[nodiscard]] auto Empty() const noexcept { return reader_->Empty(); }

    //! Returns the number of objects immediately available to read from this
    //! channel, i.e., the number of times in a row that you can call
    //! `TryReceive()` successfully.
    [[nodiscard]] auto Size() const noexcept { return reader_->Size(); }

    //! Returns true if `Close()` has been called on this channel.
    [[nodiscard]] auto Closed() const noexcept { return reader_->Closed(); }

private:
    explicit ReaderContext(detail::IntrusivePtr<detail::channel::MultiplexedReader<T>> reader)
        : reader_(std::move(reader))
    {
    }

    detail::IntrusivePtr<detail::channel::MultiplexedReader<T>> reader_;
};

//! A communication channel for broadcasting objects of type T to multiple
//! readers.
/*!
 A `BroadcastChannel` allows sending objects of type T to multiple readers. Each
 reader receives messages in its own message queue, wrapped in a
 `std::shared_ptr` to avoid expensive copies and to allow shared ownership of
 the object.

 The channel can be bounded or unbounded. A bounded channel has a maximum size,
 and attempts to send objects when the channel is full will block until space
 becomes available. An unbounded channel can grow without limit. This has the
 implication that to a certain extent, readers on the channel can operate a
 different speeds, thanks to their independent queues, but if a reader is too
 slow, causing its queue to become full, the entire channel will block until the
 reader catches up.

 The channel can be closed by calling its `Close()` method. Closing the channel
 wakes up all sleeping readers and writers with a failure indication (a null
 message), and causes all future reads and writes to fail immediately. Objects
 sent before the channel is closed can still be received.

 The `BroadcastChannel` provides separate interfaces for reading and writing:

 - `ReaderContext<T>`: Represents a reader attached to the channel. It provides
   methods to receive messages, either blocking until a message is available or
   in a non-blocking manner. It is obtained by calling `ForRead()`, and
   determines the lifetime of the reader's association to the channel.

 - `BroadcastingWriter<T>`: Represents a writer attached to the channel. It is
   obtained by calling `ForWrite()`, and provides methods to send messages to
   all readers, blocking if the channel is bounded and is full.

 \tparam T The type of the objects that can be sent and received over the
 channel.
*/
template <typename T>
class BroadcastChannel {
public:
    using Writer = detail::channel::BroadcastingWriter<T>;
    using Reader = detail::channel::MultiplexedReader<T>;

    //! Default constructor for an unbounded channel.
    BroadcastChannel() = default;

    //! Constructor for a bounded channel with a specified maximum size.
    /*!
     \param max_size The maximum number of objects that can be buffered in the
     channel. This applies to all attached readers.
    */
    explicit BroadcastChannel(const size_t max_size)
        : bounded_(true)
        , max_size_(max_size)
    {
        DCHECK_GT_F(max_size, 0UL);
    }

    //! Returns a `ReaderContext` for reading from the channel.
    /*!
     \return A `ReaderContext` object that can be used to receive messages from
     the channel.
    */
    [[nodiscard]] auto ForRead() -> ReaderContext<T>
    {
        auto reader = detail::IntrusivePtr(new detail::channel::MultiplexedReader<T>(*this));
        readers_.PushBack(*reader.Get()); // Add to intrusive list
        return ReaderContext<T>(std::move(reader));
    }

    //! Returns the `BroadcastingWriter` for writing to the channel.
    /*!
     \return A reference to the `BroadcastingWriter` object that can be used to
     send messages to the channel. This is a shared object that can be used by
     multiple writers at the same time.
    */
    [[nodiscard]] auto ForWrite() -> detail::channel::BroadcastingWriter<T>& { return writer_; }

    //! Closes the channel, waking up all readers and writers with a failure
    //! indication.
    /*!
     After the channel is closed, all future reads and writes will fail
     immediately. Messages currently buffers in the queues can still be read.
    */
    void Close()
    {
        closed_ = true;
        // Wake up all readers
        for (auto& reader : readers_) {
            reader.UnParkAll();
        }
        writer_.UnParkAll();
    }

    //! Returns true if `Close()` has been called on this channel.
    [[nodiscard]] auto Closed() const noexcept { return closed_; }

    //! Returns the number of slots immediately available to write new objects
    //! into this channel, i.e., the number of times in a row that you can call
    //! `TrySend()` on its `BroadcastWriter` successfully.
    [[nodiscard]] auto Space() const noexcept
    {
        if (!bounded_) {
            return std::numeric_limits<size_t>::max();
        }

        // Start with maximum space available
        size_t min_space = max_size_;

        // Find minimum available space across all readers
        for (const auto& reader : readers_) {
            size_t current_space = max_size_ - reader.Size();
            min_space = std::min(min_space, current_space);
        }
        return min_space;
    }

    //! Returns true if this channel contains no space for more objects, i.e., a
    //! call to `TrySend()` on its `BroadcastWriter` will return false. This may
    //! be because the channel is closed or because it has reached its capacity
    //! limit.
    [[nodiscard]] auto Full() const noexcept
    {
        return (bounded_ && Space() == 0) || closed_;
    }

    //! Returns the number of readers attached to the channel.
    [[nodiscard]] auto ReaderCount() const noexcept
    {
        return std::distance(readers_.begin(), readers_.end());
    }

private:
    friend class ReaderContext<T>;
    friend class detail::channel::BroadcastingWriter<T>;
    friend class detail::channel::MultiplexedReader<T>;

    void RemoveReader(detail::channel::MultiplexedReader<T>* reader)
    {
        detail::IntrusiveList<detail::channel::MultiplexedReader<T>>::Erase(*reader);
    }

    detail::channel::BroadcastingWriter<T> writer_ { *this };
    detail::IntrusiveList<detail::channel::MultiplexedReader<T>> readers_;
    bool closed_ { false };
    bool bounded_ { false };
    size_t max_size_ { 0 };
};

} // namespace oxygen::co
