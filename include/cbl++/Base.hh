//
// Base.hh
//
// Copyright (c) 2019 Couchbase, Inc All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include "CBLBase.h"
#include <functional>

namespace cbl {

    // Artificial base class of the C++ wrapper classes; just manages ref-counting.
    class RefCounted {
    protected:
        explicit RefCounted(CBLRefCounted *ref =nullptr) noexcept :_ref(ref) { }
        RefCounted(const RefCounted &other) noexcept     :_ref(cbl_retain(other._ref)) { }
        RefCounted(RefCounted &&other) noexcept          :_ref(other._ref) {other._ref = nullptr;}
        ~RefCounted() noexcept                           {cbl_release(_ref);}

        RefCounted& operator= (const RefCounted &other) noexcept {
            cbl_retain(other._ref);
            cbl_release(_ref);
            _ref = other._ref;
            return *this;
        }

        RefCounted& operator= (RefCounted &&other) noexcept {
            if (other._ref != _ref) {
                cbl_release(_ref);
                _ref = other._ref;
                other._ref = nullptr;
            }
            return *this;
        }

        static void check(bool ok, CBLError &error)      {if (!ok) throw error;}

        CBLRefCounted* _ref;
    };

// Internal use only: Copy/move ctors and assignment ops that have to be declared in subclasses
#define CBL_REFCOUNTED_BOILERPLATE(CLASS, SUPER, C_TYPE) \
public: \
    CLASS() noexcept                              :SUPER() { } \
    CLASS(const CLASS &other) noexcept            :SUPER(other) { } \
    CLASS(CLASS &&other) noexcept                 :SUPER((CLASS&&)other) { } \
    CLASS& operator=(const CLASS &other) noexcept {SUPER::operator=(other); return *this;} \
    CLASS& operator=(CLASS &&other) noexcept      {SUPER::operator=((SUPER&&)other); return *this;}\
    bool valid() const                            {return _ref != nullptr;} \
    explicit operator bool() const                {return valid();} \
    bool operator==(const CLASS &other) const     {return _ref == other._ref;} \
    C_TYPE* ref() const                           {return (C_TYPE*)_ref;}\
protected: \
    explicit CLASS(C_TYPE* ref)                   :SUPER((CBLRefCounted*)ref) { }



    /** A token representing a registered listener; instances are returned from the various
        methods that register listeners, such as \ref Database::addListener.
        When this object goes out of scope, the listener will be unregistered. */
    template <class... Args>
    class ListenerToken {
    public:
        using Callback = std::function<void(Args...)>;

        ListenerToken()                                  { }
        ~ListenerToken()                                 {cbl_listener_remove(_token);}

        ListenerToken(Callback cb)
        :_callback(new Callback(cb))
        { }

        ListenerToken(ListenerToken &&other)
        :_token(other._token),
        _callback(std::move(other._callback))
        {other._token = nullptr;}

        ListenerToken& operator=(ListenerToken &&other) {
            cbl_listener_remove(_token);
            _token = other._token;
            _callback = other._callback;
            other._token = nullptr;
            return *this;
        }

        /** Unregisters the listener early, before it leaves scope. */
        void remove() {
            cbl_listener_remove(_token);
            _token = nullptr;
            _callback = nullptr;
        }

        void* context()                             {return _callback.get();}
        void setToken(CBLListenerToken* token)      {assert(!_token); _token = token;}

        static void call(void* context, Args... args) {
            auto listener = (Callback*)context;
            (*listener)(args...);
        }

    private:
        CBLListenerToken* _token {nullptr};
        std::unique_ptr<Callback> _callback;

        ListenerToken(const ListenerToken&) =delete;
        ListenerToken& operator=(const ListenerToken &other) =delete;
    };


}