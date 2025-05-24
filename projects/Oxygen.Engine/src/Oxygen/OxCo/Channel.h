//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/ParkingLotImpl.h>
#include <Oxygen/OxCo/Detail/Queue.h>

// The Channel class is designed in two parts, the Reader and the Writer parts,
// and while they can be used together via the Channel interface, they can also
// be provided as separate parts to only expose one side of the channel. Hence,
// the warnings about the hidden function.

namespace oxygen::co {

template <typename T>
class Channel;

namespace detail::channel {

    template <typename T>
    class Reader;
    template <typename T>
    class Writer;

    //! Interface for reading from a Channel. Exposed publicly as
    //! Channel<T>::Reader.
    template <typename T>
    class Reader : public ParkingLotImpl<Reader<T>> {
        friend Channel<T>;
        friend Writer<T>;

        // ReSharper disable CppHiddenFunction

        [[nodiscard]] auto GetChannel() -> Channel<T>& { return static_cast<Channel<T>&>(*this); }
        [[nodiscard]] auto GetChannel() const -> const Channel<T>&
        {
            return static_cast<const Channel<T>&>(*this);
        }

        class ReadAwaiter : public Reader::ParkingLotImpl::Parked {
        public:
            using Base = typename Reader::ParkingLotImpl::Parked;

            explicit ReadAwaiter(Reader& self)
                : Base(self)
            {
            }

            [[nodiscard]] auto await_ready() const noexcept -> bool
            {
                return !GetChannel().Empty() || GetChannel().Closed();
            }

            void await_suspend(Handle h)
            {
                DLOG_F(5, "    ...channel {} receive {}", fmt::ptr(&GetChannel()), fmt::ptr(this));
                this->DoSuspend(h);
            }

            auto await_resume() -> std::optional<T> { return GetChannel().TryReceive(); }

            using Base::await_cancel;

        private:
            [[nodiscard]] auto GetChannel() -> Channel<T>&
            {
                return static_cast<Channel<T>&>(Base::Object());
            }

            [[nodiscard]] auto GetChannel() const -> const Channel<T>&
            {
                return static_cast<const Channel<T>&>(Base::Object());
            }
        };

    public:
        OXYGEN_MAKE_NON_COPYABLE(Reader)
        OXYGEN_MAKE_NON_MOVABLE(Reader)

        auto Receive() -> Awaitable<std::optional<T>> auto
        {
            return ReadAwaiter(*this);
        }

        auto TryReceive() -> std::optional<T>
        {
            std::optional<T> data;
            if (!GetChannel().Empty()) {
                data.emplace(std::move(GetChannel().buf_.Front()));
                GetChannel().buf_.PopFront();
                this->GetChannel().GetWriter().UnParkOne();
            }
            return data;
        }

        [[nodiscard]] auto Size() const noexcept -> size_t { return GetChannel().Size(); }
        // ReSharper disable once CppHidingFunction
        [[nodiscard]] auto Empty() const noexcept -> bool { return GetChannel().Empty(); }
        [[nodiscard]] auto Closed() const noexcept -> bool { return GetChannel().Closed(); }

    protected:
        // Only allow construction and destruction as a sub-object of
        // Channel; we assume we can't exist as a standalone object.
        Reader() = default;
        ~Reader() override = default;

        [[nodiscard]] auto HasWaiters() const noexcept
        {
            return !this->ParkingLotImpl<Reader>::Empty();
        }
    };

    //! Interface for writing to a Channel. Exposed publicly as
    //! Channel<T>::Writer.
    template <typename T>
    class Writer : public ParkingLotImpl<Writer<T>> {
        friend Channel<T>;
        friend Reader<T>;

        [[nodiscard]] auto GetChannel() -> Channel<T>&
        {
            return static_cast<Channel<T>&>(*this);
        }
        [[nodiscard]] auto GetChannel() const -> const Channel<T>&
        {
            return static_cast<const Channel<T>&>(*this);
        }

        template <typename U>
        class WriteAwaiter : public Writer::ParkingLotImpl::Parked {
        public:
            using Base = typename Writer::ParkingLotImpl::Parked;

            // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
            WriteAwaiter(Writer& self, U&& data) // Perfect forwarding
                : Base(self)
                , data_(std::forward<U>(data))
            {
            }

            [[nodiscard]] auto await_ready() const noexcept -> bool
            {
                return GetChannel().Closed() || !GetChannel().Full();
            }

            void await_suspend(Handle h)
            {
                DLOG_F(5, "    ...channel {} send {}", fmt::ptr(&GetChannel()), fmt::ptr(this));
                this->DoSuspend(h);
            }

            auto await_resume() -> bool
            {
                return GetChannel().TrySend(std::forward<U>(data_));
            }

            using Base::await_cancel;

        private:
            [[nodiscard]] auto GetChannel() -> Channel<T>&
            {
                return static_cast<Channel<T>&>(Base::Object());
            }
            [[nodiscard]] auto GetChannel() const -> const Channel<T>&
            {
                return static_cast<const Channel<T>&>(Base::Object());
            }

            U&& data_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        };

    public:
        OXYGEN_MAKE_NON_COPYABLE(Writer)
        OXYGEN_MAKE_NON_MOVABLE(Writer)

        template <typename U>
        auto Send(U&& value) -> Awaitable<bool> auto
        {
            return WriteAwaiter<U>(*this, std::forward<U>(value));
        }
        template <typename U>
        auto TrySend(U&& value) -> bool
        {
            if (GetChannel().Closed() || GetChannel().Full()) {
                return false;
            }
            GetChannel().buf_.PushBack(std::forward<U>(value));
            GetChannel().GetReader().UnParkOne();
            return true;
        }
        void Close() { GetChannel().Close(); }

        [[nodiscard]] auto Space() const noexcept { return GetChannel().Space(); }
        [[nodiscard]] auto Full() const noexcept { return GetChannel().Full(); }
        [[nodiscard]] auto Closed() const noexcept { return GetChannel().Closed(); }

    protected:
        // Only allow construction and destruction as a sub-object of
        // Channel; we assume we can't exist as a standalone object.
        Writer() = default;
        ~Writer() override = default;

        [[nodiscard]] auto HasWaiters() const noexcept -> bool
        {
            return !this->ParkingLotImpl<Writer>::Empty();
        }
    };

} // namespace detail::channel

//! An ordered communication channel for sending objects of type T between
//! tasks.
/*!
 Each Channel has an internal buffer to store the objects that have been sent
 but not yet received. This buffer may be allowed to grow without bound (the
 default) or may be limited to a specific size. Because every object sent
 through the channel must pass through the buffer on its way from the sending
 task to the receiving task, a buffer size of zero is nonsensical and is
 forbidden at construction time. In general, if you want to be able to send N
 objects in a row without blocking, you must allow for a buffer size of at least
 N. Even if there are tasks waiting to immediately receive each object you
 enqueue, the objects will still be stored in the buffer until the next tick of
 the executor.

 A Channel can be closed by calling its `Close()` method. Closing the channel
 causes all sleeping readers and writers to wake up with a failure indication,
 and causes all future reads and writes to fail immediately rather than
 blocking. If you send some objects through a channel and then close it, the
 objects that were sent before the closure can still be validly received.

 \note Destroying the Channel object is _not_ equivalent to closing it; a
       destroyed Channel must not have any readers or writers still waiting on
       it.

 A Channel acts as if it is made up of two parts, one for reading and one for
 writing, which can be referenced separately. For example, if you want to allow
 someone to write to your channel but not read from it, you can pass them a
 `Channel<T>::Writer&` and they will only have access to the writing-related
 methods. The `Reader&` and `Writer&` types are implicitly convertible from
 `Channel&`, or you can use the `GetReader()` and `GetWriter()` methods to
 obtain them.
*/
template <typename T>
class Channel : public detail::channel::Reader<T>,
                public detail::channel::Writer<T> {
    // ReSharper disable CppHidingFunction
public:
    //! Constructs an unbounded channel. No initial capacity is allocated;
    //! later channel operations will need to allocate to do their work.
    Channel() = default;

    //! Constructs a bounded channel. Space for `max_size` buffered objects
    //! of type `T` will be allocated immediately, and no further allocations
    //! will be performed.
    explicit Channel(size_t max_size)
        : buf_(max_size)
        , bounded_(true)
    {
        DCHECK_GT_F(max_size, 0UL);
    }

    //! Detect some uses of 'Channel(0)' and fail at compile time.
    /*!
     A literal `0` has conversions of equal rank to size_t and `nullptr_t`.
    */
    explicit Channel(std::nullptr_t) = delete;

    //! Verify that no tasks are still waiting on this Channel when it is about
    //! to be destroyed.
    ~Channel() override
    {
        DCHECK_F(!GetReader().HasWaiters(), "Still some tasks suspended while reading from this channel");
        DCHECK_F(!GetWriter().HasWaiters(), "Still some tasks suspended while writing to this channel");
    }

    OXYGEN_MAKE_NON_COPYABLE(Channel)
    OXYGEN_MAKE_NON_MOVABLE(Channel)

    //! Returns the number of objects immediately available to read from this
    //! channel, i.e., the number of times in a row that you can call
    //! `TryReceive()` successfully.
    [[nodiscard]] auto Size() const noexcept { return buf_.Size(); }

    //! Returns true if this channel has no buffered objects, i.e., a call to
    //! TryReceive() will return `std::nullopt`.
    [[nodiscard]] auto Empty() const noexcept { return buf_.Empty(); }

    //! Returns the number of slots immediately available to write new objects
    //! into this channel, i.e., the number of times in a row that you can call
    //! `TrySend()` successfully.
    [[nodiscard]] auto Space() const noexcept
    {
        if (closed_) {
            return 0;
        }
        if (!bounded_) {
            return std::numeric_limits<size_t>::max();
        }
        return buf_.Capacity() - buf_.Size();
    }

    //! Returns true if this channel contains no space for more objects, i.e., a
    //! call to `TrySend()` will return false. This may be because the channel
    //! is closed or because it has reached its capacity limit.
    [[nodiscard]] auto Full() const noexcept
    {
        return (bounded_ && buf_.Capacity() == buf_.Size()) || closed_;
    }

    //! Returns true if `Close()` has been called on this channel.
    [[nodiscard]] auto Closed() const noexcept { return closed_; }

    //! Closes the channel. No more data can be written to the channel. All
    //! queued writes are aborted. Any suspended reads will still be able to
    //! read whatever data remains in the channel.
    void Close()
    {
        closed_ = true;
        GetReader().UnParkAll();
        GetWriter().UnParkAll();
    }

    //! A reference to this channel that only exposes the operations that would
    //! be needed by a reader: `Receive()`, `TryReceive()`, `Size()`, `Empty()`,
    //! and `Closed()`.
    using Reader = detail::channel::Reader<T>;
    [[nodiscard]] auto GetReader() -> Reader& { return static_cast<Reader&>(*this); }
    [[nodiscard]] auto GetReader() const -> const Reader&
    {
        return static_cast<const Reader&>(*this);
    }

    //! Retrieve and return an object from the channel, blocking if no objects
    //! are immediately available. Returns `std::nullopt` if the channel is
    //! closed and has no objects left to read.
    auto Receive() -> Awaitable<std::optional<T>> auto
    {
        return GetReader().Receive();
    }

    //! Retrieve and return an object from the channel, or return `std::nullopt`
    //! if none are immediately available.
    auto TryReceive() -> std::optional<T> { return GetReader().TryReceive(); }

    //! A reference to this channel that only exposes the operations that would
    //! be needed by a writer: `Send()`, `TrySend()`, `Close()`, `Space()`,
    //! `Full()`, and `Closed()`.
    using Writer = detail::channel::Writer<T>;
    [[nodiscard]] auto GetWriter() -> Writer& { return static_cast<Writer&>(*this); }
    [[nodiscard]] auto GetWriter() const -> const Writer&
    {
        return static_cast<const Writer&>(*this);
    }

    //! Deliver an object to the channel, blocking if there is no space
    //! available in the buffer.
    /*!
     \return `false` if the channel cannot accept more incoming objects because it
     has been closed, `true` if the object was delivered.
    */
    template <typename U>
    auto Send(U&& value) -> Awaitable<bool> auto
    {
        return GetWriter().Send(std::forward<U>(value));
    }

    //! Deliver an object to the channel if there is space immediately available
    //! for it in the buffer.
    /*!
     \return `true` if the object was delivered, `false` if there was no space
     or the channel was closed.
     */
    template <typename U>
    auto TrySend(U&& value) -> bool
    {
        return GetWriter().TrySend(std::forward<U>(value));
    }

private:
    friend Reader;
    friend Writer;

    detail::Queue<T> buf_ { 0 };
    bool closed_ { false };
    bool bounded_ { false };
};

} // namespace oxygen::co
