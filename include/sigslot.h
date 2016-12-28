#pragma once

#include <memory>
#include <functional>
#include <list>
#include <atomic>
#include <mutex>
#include <algorithm>


namespace nsSigslot
{
	class ObjectBase
	{
	protected:
		std::atomic<bool> enable_ = true;
	public:
		virtual ~ObjectBase(){}
		void Enable(bool enable=true){enable_ = enable;}
		bool Enabled(){return enable_;}
	};

	template<typename Signature>
	class Connection:
		public ObjectBase
	{
		template<typename Signature, typename Mutex>
		friend class Signal;
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

	template<typename Signature, typename Mutex = std::recursive_mutex>
	class Signal :
		public ObjectBase
	{
		Mutex lock_;
		std::list<std::weak_ptr<Connection<Signature>>> conns_;
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
		auto Connect(std::function<Signature> func) -> std::shared_ptr<Connection<Signature>>
		{
			auto conn = std::make_shared<Connection<Signature>>();
			conn->slot_ = func;

			std::lock_guard<decltype(lock_)> l(lock_);
			conns_.push_back(conn);
			return conn;
		}
		// this is not required, you can reset conn to disconnect
//		void Disconnect(std::shared_ptr<Connection<Signature>> conn)
//		{
//			std::lock_guard<decltype(lock_)> l(lock_);
//			auto iter = std::find_if(conns_.begin(), conns_.end(), [&conn](std::weak_ptr<Connection<Signature>> l){
//				auto lconn = l.lock();
//				return (lconn == conn);
//			});
//			if (conns_.end() != iter)
//				conns_.erase(iter);
//		}
//		void DisconnectAll()
//		{
//			std::lock_guard<decltype(lock_)> l(lock_);
//			conns_.clear();
//		}
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
};
