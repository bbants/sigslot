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
		std::function<void(void*)> deleter_;// we need to clear this before releasing a weak pointer
	public:
		virtual ~ObjectBase(){}
		virtual void OnFinal(){deleter_ = nullptr;}
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
		typedef std::pair<void*, std::weak_ptr<Connection<Signature>>> WeakConnection;
		template<typename Mutex>
		friend class SignalHub;
		Mutex lock_;
		std::list<WeakConnection> conns_;
#if defined(_DEBUG) || defined(DEBUG)
		std::map<std::string, WeakConnection> named_conns_;
#endif
	public:
		~Signal()
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			for (auto&& conn : conns_)
			{
				auto locked_conn = conn.second.lock();
				if (locked_conn)
				{// clear the callback
					locked_conn->OnFinal();
				}
			}
		}
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

				auto conn = it->second.lock();
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
			auto conn = new Connection<Signature>();
			conn->slot_ = func;
			conn->name_ = name;
			conn->sig_name_ = name_;

			auto result = std::shared_ptr<Connection<Signature>>(conn, [](Connection<Signature>* p)
			{
				if (p->deleter_)
					p->deleter_(p);
				delete p;
			});
			ConnectInternal(conn,result);
			return result;
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
		void ConnectInternal(void* p, std::shared_ptr<Connection<Signature>> conn)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			conns_.push_back(std::make_pair(p,conn));

			// replace the deleter
			auto name = conn->Name();
			conn->deleter_ = [this, name](void* p)
			{
				std::lock_guard<decltype(lock_)> l(lock_);
				auto conn_iter = std::find_if(conns_.begin(), conns_.end(), [p](WeakConnection iter) -> bool
				{
					return iter.first == p;
				});
				if (conn_iter != conns_.end())
					conns_.erase(conn_iter);
#if defined(_DEBUG) || defined(DEBUG)
				if (name.size())
				{
					named_conns_.erase(name);
				}
#endif
			};

#if defined(_DEBUG) || defined(DEBUG)
			if (name.size())
			{
				auto conn_iter = named_conns_.find(name);
				if (conn_iter != named_conns_.end())
				{
					auto old_conn = conn_iter->second.second.lock();
					assert(nullptr == old_conn); // an old instance is still valid
				}
				named_conns_[name] = std::make_pair(p, conn);
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
		typedef std::pair<void*, std::weak_ptr<SignalBase>> WeakSignal;
		typedef std::pair<void*, std::weak_ptr<ConnectionBase>> WeakConnection;
		Mutex lock_;
		std::map<std::string, WeakSignal> signals_;
		std::map<std::string, std::list<WeakConnection>>  early_conns_;
	public:
		SignalHub()
		{}
		~SignalHub()
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			for (auto&& iter : signals_)
			{
				auto&& item = iter.second;
				auto locked_item = item.second.lock();
				if (locked_item)
				{
					locked_item->OnFinal();
				}
			}
			signals_.clear();
			for (auto&& iter : early_conns_)
			{
				auto&& conns = iter.second;
				for (auto&& item : conns)
				{
					auto locked_item = item.second.lock();
					if (locked_item)
					{
						locked_item->OnFinal();
					}
				}
			}
			early_conns_.clear();
		}

		/*
		save the return value to keep the signal valid
		sig_name : required, unique, used to bind sig-slot
		*/
		template<typename Signature>
		auto AddSignal(std::string sig_name) -> std::shared_ptr<Signal<Signature,Mutex>>
		{
			auto signal = new Signal<Signature, Mutex>();
			signal->name_ = sig_name;

			auto conns_iter = early_conns_.find(sig_name);
			if (conns_iter != early_conns_.end())
			{
				auto&& conns = conns_iter->second;
				for (auto&& conn : conns)
				{
					auto p = conn.first;
					auto tmp = conn.second.lock();
					if (nullptr != tmp)
					{
						signal->ConnectInternal(p, std::dynamic_pointer_cast<Connection<Signature>>(tmp));
					}
				}
				early_conns_.erase(conns_iter);
			}

			signal->deleter_ = [this, sig_name](void* p)
			{
				std::lock_guard<decltype(lock_)> l(lock_);
				auto sig_iter = signals_.find(sig_name);
				if (signals_.end() != sig_iter)
				{
					if (p == sig_iter->second.first)
					{
						// we are the last one
						signals_.erase(sig_iter);
					}
				}
			};

			auto result = std::shared_ptr<Signal<Signature, Mutex>>(signal, [](Signal<Signature, Mutex>* p)
			{
				// do not refer to this in destructor, instead, place clean logic in deleter_
				if (p->deleter_)
					p->deleter_(p);
				delete p;
			});

			std::lock_guard<decltype(lock_)> l(lock_);
			signals_[sig_name] = std::make_pair(signal,result);
			return result;
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
				auto signal = sig_iter->second.second.lock();
				if (nullptr != signal)
				{
					return std::dynamic_pointer_cast<Signal<Signature, Mutex>>(signal)->Connect(func, slot_name);
				}
			}

			// the signal is not available now, store to a weak_ptr first
			auto conn = new Connection<Signature>();
			conn->slot_ = func;
			conn->name_ = slot_name;
			conn->sig_name_ = sig_name;
			conn->deleter_ = [this, sig_name](void* p)
			{
				std::lock_guard<decltype(lock_)> l(lock_);
				auto conns_iter = early_conns_.find(sig_name);
				if (conns_iter != early_conns_.end())
				{
					auto&& conns = conns_iter->second;
					auto conn_iter = std::find_if(conns.begin(), conns.end(), [p](WeakConnection iter) -> bool
					{
						return iter.first == p;
					});
					if (conn_iter != conns.end())
					{
						conns.erase(conn_iter);
					}
				}
			};

			auto result = std::shared_ptr<Connection<Signature>>(conn, [](Connection<Signature>* p)
			{
				if (p->deleter_)
					p->deleter_(p);
				delete p;
			});
			early_conns_[sig_name].push_back(std::make_pair(conn, std::weak_ptr<Connection<Signature>>(result)));
			return result;
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

				signal = sig_iter->second.second.lock();
				if (nullptr == signal)
					return;
			}
			(*std::dynamic_pointer_cast<Signal<Signature, Mutex>>(signal))(params...);
		}
	};
}
