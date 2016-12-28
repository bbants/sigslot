#pragma once

#include <memory>
#include <functional>
#include <list>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <string>
#include <map>

namespace nsNamedSigslot
{
	class ObjectBase
	{
	protected:
		std::atomic<bool> enable_ = true;
	public:
		virtual ~ObjectBase(){}
		void Enable(bool enable = true){ enable_ = enable; }
		bool Enabled(){ return enable_; }
	};

	class NamedObjectBase :
		public ObjectBase
	{
	protected:
		std::string name_;
	public:
		std::string Name(){ return name_; }
	};

	class ConnectionBase :
		public NamedObjectBase
	{
	protected:
		std::string sig_name_;
	public:
		std::string SigName(){ return sig_name_; }
	};

	template<typename Signature>
	class Connection:
		public ConnectionBase
	{
		template<typename Signature, typename Mutex>
		friend class Signal;
		template<typename Mutex>
		friend class SignalHub;
		std::function<Signature> slot_;

		template <typename ...Params>
		void operator()(Params&&... params)
		{
			if (!Enabled())
				return;
			slot_(params...);
		}
		template <typename ...Params>
		void Emit(Params&&... params)
		{
			operator()(params...);
		}
	};

	class SignalBase :
		public NamedObjectBase
	{
	};

	template<typename Signature, typename Mutex = std::recursive_mutex>
	class Signal :
		public SignalBase
	{
		template<typename Mutex>
		friend class SignalHub;
		Mutex lock_;
		std::list<std::weak_ptr<Connection<Signature>>> conns_;
#if defined(_DEBUG) || defined(DEBUG)
		std::map<std::string, std::weak_ptr<Connection<Signature>>> named_conns_;
#endif
	public:
		template <typename ...Params>
		void operator()(Params&&... params)
		{
			if (!Enabled())
				return;

			std::lock_guard<decltype(lock_)> l(lock_);
			auto it = conns_.begin();
			auto itEnd = conns_.end();

			while (it != itEnd)
			{
				auto itNext = it;
				++itNext;

				auto conn = it->lock();
				if (conn)
					(*conn)(params...);
				else
					conns_.erase(it);

				it = itNext;
			}
		}
		template <typename ...Params>
		void Emit(Params&&... params)
		{
			operator()(params...);
		}
		// save the return value as long as you want to keep the connection
		auto Connect(std::function<Signature> func, std::string name = "") -> std::shared_ptr<Connection<Signature>>
		{
			auto conn = std::make_shared<Connection<Signature>>();
			conn->slot_ = func;
			conn->name_ = name;
			conn->sig_name_ = name_;

			ConnectInternal(conn);
			return conn;
		}
		// this is not required, you can reset conn to disconnect
// 		void Disconnect(std::shared_ptr<Connection<Signature>> conn)
// 		{
// 			std::lock_guard<decltype(lock_)> l(lock_);
// 			auto iter = std::find_if(conns_.begin(), conns_.end(), [&conn](std::weak_ptr<Connection<Signature>> l){
// 				auto lconn = l.lock();
// 				return (lconn == conn);
// 			});
// 			if (conns_.end() != iter)
// 				conns_.erase(iter);
// 		}
// 		void DisconnectAll()
// 		{
// 			std::lock_guard<decltype(lock_)> l(lock_);
// 			conns_.clear();
// 		}
	protected:
		void ConnectInternal(std::weak_ptr<Connection<Signature>> conn)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			conns_.push_back(conn);

#if defined(_DEBUG) || defined(DEBUG)
			auto conn_locked = conn.lock();
			assert(conn_locked != nullptr);
			auto name = conn_locked->Name();
			if (name.size())
			{
				auto conn_iter = named_conns_.find(name);
				if (conn_iter != named_conns_.end())
				{
					auto old_conn = conn_iter->second.lock();
					assert(nullptr == old_conn); // an old instance is still valid
				}
				named_conns_[name] = conn;
			}
#endif
		}
	};
	// a utility class to hold all connections/signals
	template<typename Element, typename Mutex = std::recursive_mutex>
	class ObjectContainer
	{
		Mutex lock_;
		std::list<std::shared_ptr<Element>> items_;
	public:
		void Save(std::shared_ptr<Element> item)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			items_.push_back(item);
		}
		void Enable(bool enable=true)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			for (auto&& item : items_)
				item->Enable(enable);
		}
		template<typename Comp>
		void EnableIf(Comp comp, bool enable=true)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			for (auto&& item : items_)
			{
				if (comp(item))
				{
					item->Enable(enable);
				}
			}
		}
		// use std::shared_ptr, release the object to disconnect all
// 		void DisconnectAll()
// 		{
// 			std::lock_guard<decltype(lock_)> l(lock_);
// 			items_.clear();
// 		}
	};

	template<typename Mutex = std::recursive_mutex>
	class SignalHub
	{
		Mutex lock_;
		std::map<std::string, std::weak_ptr<SignalBase>> signals_;
		std::map<std::string, std::list<std::weak_ptr<ConnectionBase>>>  early_conns_;
	public:
		SignalHub()
		{}
		~SignalHub()
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			signals_.clear();
		}

		/*
		save the return value to keep the signal valid
		sig_name : required, unique, used to bind sig-slot
		*/
		template<typename Signature>
		auto AddSignal(std::string sig_name) -> std::shared_ptr<Signal<Signature,Mutex>>
		{
			auto signal = std::make_shared<Signal<Signature, Mutex>>();
			signal->name_ = sig_name;

			std::lock_guard<decltype(lock_)> l(lock_);
			signals_[sig_name] = signal;

			auto conn_iter = early_conns_.find(sig_name);
			if (conn_iter != early_conns_.end())
			{
				auto&& conns = conn_iter->second;
				for (auto&& conn : conns)
				{
					auto tmp = conn.lock();
					if (nullptr != tmp)
					{
						signal->ConnectInternal(std::dynamic_pointer_cast<Connection<Signature>>(tmp));
					}
				}
				early_conns_.erase(conn_iter);
			}
			return signal;
		}

		/*
		save the return value as long as you want to keep the connection
		sig_name : required, unique, used to bind sig-slot
		slot_name : optional, for debug purpose
		*/
		template<typename Signature>
		auto Connect(std::string sig_name, std::function<Signature> func, std::string slot_name = "") -> std::shared_ptr<Connection<Signature>>
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			auto sig_iter = signals_.find(sig_name);
			if (signals_.end() != sig_iter)
			{
				auto signal = sig_iter->second.lock();
				if (nullptr != signal)
				{
					return std::dynamic_pointer_cast<Signal<Signature, Mutex>>(signal)->Connect(func, slot_name);
				}
			}

			// the signal is not available now, store to a weak_ptr first
			auto conn = std::make_shared<Connection<Signature>>();
			conn->slot_ = func;
			conn->name_ = slot_name;
			conn->sig_name_ = sig_name;
			early_conns_[sig_name].push_back(conn);
			return conn;
		}

		template <typename Signature, typename ...Params>
		void Emit(std::string sig_name, Params&&... params)
		{
			std::shared_ptr<SignalBase> signal = nullptr;
			{
				std::lock_guard<decltype(lock_)> l(lock_);
				auto sig_iter = signals_.find(sig_name);
				if (signals_.end() == sig_iter)
					return;

				signal = sig_iter->second.lock();
				if (nullptr == signal)
					return;
			}
			(*std::dynamic_pointer_cast<Signal<Signature, Mutex>>(signal))(params...);
		}
	};
}
